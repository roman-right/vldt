#include "validation.hpp"

#include "conversion/rapidjson_to_pyobject.hpp"
#include "data_model.hpp"
#include "error_handling.hpp"
#include "init_globals.hpp"
#include "schema/deserializer.hpp"
#include "schema/schema.hpp"
#include "validation_validators.hpp"

#include <Python.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <rapidjson/document.h>
#include <string>

extern PyObject *empty_tuple;

/**
 * @brief Materialize a single rapidjson value as a Python object.
 *
 * Helper used by the slow paths in validate_and_convert_from_json (custom
 * deserializers, Union fallback, type coercion). The slow path is a strict
 * superset of the dict-based behaviour: by materializing the value and
 * delegating to validate_and_convert we keep semantics identical to the
 * pre-existing path.
 */
static PyObject *materialize(const rapidjson::Value &val) {
  return rapidjson_to_pyobject(val);
}

/**
 * @brief Forward declaration so init_from_json can recurse into nested models.
 *
 * Defined later in this file. Exported via validation.hpp so json_utils.cpp
 * can use it as the top-level entry point for from_json.
 */
PyObject *data_model_from_json(PyObject *cls,
                               const rapidjson::Value &json_obj,
                               ErrorCollector *outer_collector,
                               const char *error_path);

/**
 * @brief Validate a JSON array against a list[T] schema.
 */
static PyObject *validate_list_from_json(const rapidjson::Value &val,
                                         TypeSchema *ts,
                                         ErrorCollector *collector,
                                         const char *error_path,
                                         Deserializers *deserializers) {
  if (!val.IsArray()) {
    if (collector) {
      collector->add_error(error_path, "Expected a list");
    }
    return nullptr;
  }
  rapidjson::SizeType size = val.Size();
  PyObject *new_list = PyList_New(size);
  if (!new_list) {
    return nullptr;
  }

  size_t base_len = std::strlen(error_path);
  std::array<char, 256> new_path;
  if (base_len >= new_path.size() - 2) {
    base_len = new_path.size() - 2;
  }
  std::memcpy(new_path.data(), error_path, base_len);
  new_path[base_len] = '.';
  new_path[base_len + 1] = '\0';

  for (rapidjson::SizeType i = 0; i < size; i++) {
    std::snprintf(new_path.data() + base_len + 1,
                  new_path.size() - base_len - 1, "%u", i);
    PyObject *item = validate_and_convert_from_json(
        val[i], ts->args[0], collector, new_path.data(), deserializers);
    if (!item) {
      Py_DECREF(new_list);
      return nullptr;
    }
    PyList_SET_ITEM(new_list, i, item);
  }
  return new_list;
}

/**
 * @brief Validate a JSON object against a dict[K, V] schema.
 *
 * Only string keys are produced from JSON, so the key schema is checked
 * against the JSON-decoded string (which then runs through normal
 * validate_and_convert).
 */
static PyObject *validate_dict_from_json(const rapidjson::Value &val,
                                         TypeSchema *ts,
                                         ErrorCollector *collector,
                                         const char *error_path,
                                         Deserializers *deserializers) {
  if (!val.IsObject()) {
    if (collector) {
      collector->add_error(error_path, "Expected a dict");
    }
    return nullptr;
  }
  PyObject *new_dict = PyDict_New();
  if (!new_dict) {
    return nullptr;
  }

  TypeSchema *key_schema = ts->args[0];
  TypeSchema *val_schema = ts->args[1];

  size_t base_len = std::strlen(error_path);
  std::array<char, 256> new_path;
  if (base_len >= new_path.size() - 2) {
    base_len = new_path.size() - 2;
  }
  std::memcpy(new_path.data(), error_path, base_len);
  new_path[base_len] = '.';
  new_path[base_len + 1] = '\0';

  for (auto itr = val.MemberBegin(); itr != val.MemberEnd(); ++itr) {
    const char *key_str = itr->name.GetString();
    rapidjson::SizeType key_len = itr->name.GetStringLength();
    std::snprintf(new_path.data() + base_len + 1,
                  new_path.size() - base_len - 1, "%s", key_str);

    PyObject *py_key = PyUnicode_FromStringAndSize(key_str, key_len);
    if (!py_key) {
      Py_DECREF(new_dict);
      return nullptr;
    }
    PyObject *conv_key = validate_and_convert(py_key, key_schema, collector,
                                              new_path.data(), deserializers);
    Py_DECREF(py_key);
    if (!conv_key) {
      Py_DECREF(new_dict);
      return nullptr;
    }

    PyObject *conv_val = validate_and_convert_from_json(
        itr->value, val_schema, collector, new_path.data(), deserializers);
    if (!conv_val) {
      Py_DECREF(conv_key);
      Py_DECREF(new_dict);
      return nullptr;
    }
    if (PyDict_SetItem(new_dict, conv_key, conv_val) < 0) {
      Py_DECREF(conv_key);
      Py_DECREF(conv_val);
      Py_DECREF(new_dict);
      return nullptr;
    }
    Py_DECREF(conv_key);
    Py_DECREF(conv_val);
  }
  return new_dict;
}

/**
 * @brief Validate a JSON array against a Tuple[T1, T2, ...] schema.
 */
static PyObject *validate_tuple_from_json(const rapidjson::Value &val,
                                          TypeSchema *ts,
                                          ErrorCollector *collector,
                                          const char *error_path,
                                          Deserializers *deserializers) {
  if (!val.IsArray()) {
    if (collector) {
      collector->add_error(error_path, "Expected a tuple");
    }
    return nullptr;
  }
  rapidjson::SizeType size = val.Size();
  if (ts->num_args != static_cast<Py_ssize_t>(size)) {
    if (collector) {
      std::array<char, 128> buf;
      std::snprintf(buf.data(), buf.size(),
                    "Expected tuple of length %zd, got %u", ts->num_args, size);
      collector->add_error(error_path, buf.data());
    }
    return nullptr;
  }
  PyObject *new_tuple = PyTuple_New(size);
  if (!new_tuple) {
    return nullptr;
  }
  for (rapidjson::SizeType i = 0; i < size; i++) {
    std::array<char, 256> new_path;
    std::snprintf(new_path.data(), new_path.size(), "%s.%u", error_path, i);
    PyObject *conv = validate_and_convert_from_json(
        val[i], ts->args[i], collector, new_path.data(), deserializers);
    if (!conv) {
      Py_DECREF(new_tuple);
      return nullptr;
    }
    PyTuple_SET_ITEM(new_tuple, i, conv);
  }
  return new_tuple;
}

/**
 * @brief Validate a JSON array against a Set[T] schema.
 */
static PyObject *validate_set_from_json(const rapidjson::Value &val,
                                        TypeSchema *ts,
                                        ErrorCollector *collector,
                                        const char *error_path,
                                        Deserializers *deserializers) {
  if (!val.IsArray()) {
    if (collector) {
      collector->add_error(error_path, "Expected a set");
    }
    return nullptr;
  }
  PyObject *new_set = PySet_New(nullptr);
  if (!new_set) {
    return nullptr;
  }
  for (rapidjson::SizeType i = 0; i < val.Size(); i++) {
    std::array<char, 256> new_path;
    std::snprintf(new_path.data(), new_path.size(), "%s.%u", error_path, i);
    PyObject *conv = validate_and_convert_from_json(
        val[i], ts->args[0], collector, new_path.data(), deserializers);
    if (!conv) {
      Py_DECREF(new_set);
      return nullptr;
    }
    if (PySet_Add(new_set, conv) < 0) {
      Py_DECREF(conv);
      Py_DECREF(new_set);
      return nullptr;
    }
    Py_DECREF(conv);
  }
  return new_set;
}

/**
 * @brief Validate a JSON value against a Union schema.
 *
 * Strategy: try each candidate in order. For each, we run
 * validate_and_convert_from_json with a temporary collector so the partial
 * errors of failing branches do not leak into the main collector.
 */
static PyObject *validate_union_from_json(const rapidjson::Value &val,
                                          TypeSchema *ts,
                                          ErrorCollector *collector,
                                          const char *error_path,
                                          Deserializers *deserializers) {
  if (val.IsNull()) {
    if (ts->is_optional) {
      Py_INCREF(Py_None);
      return Py_None;
    }
  }
  ErrorCollector temp;
  for (Py_ssize_t i = 0; i < ts->num_args; i++) {
    PyObject *conv = validate_and_convert_from_json(
        val, ts->args[i], &temp, error_path, deserializers);
    if (conv) {
      return conv;
    }
    PyErr_Clear();
  }
  if (collector) {
    collector->add_error(
        error_path, "Value did not match any candidate in Union");
  }
  return nullptr;
}

/**
 * @brief Public entry point: validate a rapidjson value against a TypeSchema.
 *
 * Fast path for matching primitive types and structural matches; falls back
 * to materialize + validate_and_convert for type-coercion or
 * deserializer-based paths. The fallback ensures behaviour is identical to
 * the existing dict-based pipeline.
 */
PyObject *validate_and_convert_from_json(const rapidjson::Value &val,
                                         TypeSchema *ts,
                                         ErrorCollector *collector,
                                         const char *error_path,
                                         Deserializers *deserializers) {
  // None / Optional handling.
  if (val.IsNull()) {
    if (ts->is_optional || ts->expected_type == AnyType) {
      Py_INCREF(Py_None);
      return Py_None;
    }
    // Fall through; non-Optional fields will report a type mismatch via the
    // materialized fallback below.
  }

  // Any: materialize directly.
  if (ts->expected_type == AnyType) {
    return materialize(val);
  }

  // Nested DataModel.
  if (ts->is_data_model && val.IsObject()) {
    return data_model_from_json(ts->expected_type, val, collector, error_path);
  }

  // Containers.
  switch (ts->container_kind) {
  case CK_LIST:
    return validate_list_from_json(val, ts, collector, error_path,
                                   deserializers);
  case CK_DICT:
    return validate_dict_from_json(val, ts, collector, error_path,
                                   deserializers);
  case CK_TUPLE:
    return validate_tuple_from_json(val, ts, collector, error_path,
                                    deserializers);
  case CK_SET:
    return validate_set_from_json(val, ts, collector, error_path,
                                  deserializers);
  case CK_UNION:
    return validate_union_from_json(val, ts, collector, error_path,
                                    deserializers);
  }

  // Primitive fast paths. Match the JSON type to the expected Python type and
  // emit a single allocation per leaf.
  if (ts->expected_type == IntType) {
    if (val.IsInt()) {
      return PyLong_FromLong(val.GetInt());
    }
    if (val.IsInt64()) {
      return PyLong_FromLongLong(val.GetInt64());
    }
    if (val.IsUint()) {
      return PyLong_FromUnsignedLong(val.GetUint());
    }
    if (val.IsUint64()) {
      return PyLong_FromUnsignedLongLong(val.GetUint64());
    }
    // Fall through to the coercion path below.
  } else if (ts->expected_type == FloatType) {
    if (val.IsDouble() || val.IsLosslessDouble()) {
      return PyFloat_FromDouble(val.GetDouble());
    }
    if (val.IsInt()) {
      return PyFloat_FromDouble(static_cast<double>(val.GetInt()));
    }
    if (val.IsInt64()) {
      return PyFloat_FromDouble(static_cast<double>(val.GetInt64()));
    }
  } else if (ts->expected_type == StrType) {
    if (val.IsString()) {
      return PyUnicode_FromStringAndSize(val.GetString(),
                                         val.GetStringLength());
    }
  } else if (ts->expected_type == BoolType) {
    if (val.IsBool()) {
      if (val.GetBool()) {
        Py_RETURN_TRUE;
      }
      Py_RETURN_FALSE;
    }
  }

  // Coercion / deserializer / non-matching primitive path: materialize the
  // value and delegate to the existing validator. This preserves the full
  // semantics of the dict-based pipeline (custom deserializers, str -> int
  // fallback, etc).
  PyObject *as_py = materialize(val);
  if (!as_py) {
    return nullptr;
  }
  PyObject *result = validate_and_convert(as_py, ts, collector, error_path,
                                          deserializers);
  Py_DECREF(as_py);
  return result;
}

/**
 * @brief Construct a DataModel directly from a rapidjson object.
 *
 * Allocates a fresh instance of `cls`, populates instance_data->fields by
 * walking the schema and looking up each field in the JSON object (or the
 * fallback alias list), and runs the standard validator hooks.
 *
 * If the model declares model_before or field_before validators, those need
 * to operate on Python objects, so we fall back to materializing a Python
 * dict and calling the existing DataModel_init pipeline. This keeps validator
 * semantics identical.
 */
PyObject *data_model_from_json(PyObject *cls,
                               const rapidjson::Value &json_obj,
                               ErrorCollector *outer_collector,
                               const char *error_path) {
  PyObject *capsule = get_schema_cached(cls);
  if (!capsule) {
    return nullptr;
  }
  SchemaCache *schema =
      (SchemaCache *)PyCapsule_GetPointer(capsule, "vldt.SchemaCache");
  Py_DECREF(capsule);
  if (!schema) {
    return nullptr;
  }

  // Subclasses that override __init__ in Python (most importantly
  // AsyncDataModel, which stashes the kwargs into _init_kwargs and runs the
  // validation pipeline on await) need cls.__call__ to dispatch through the
  // Python override. The C tp_init of those classes points to slot_tp_init
  // rather than DataModel_init, so detect that and fall back.
  PyTypeObject *cls_type = (PyTypeObject *)cls;
  bool needs_python_init = (cls_type->tp_init != DataModel_init);

  // Validators that take a Python dict / Python value need the fallback path.
  if (needs_python_init || schema->has_model_before ||
      schema->has_field_before) {
    PyObject *dict_obj = rapidjson_to_pyobject(json_obj);
    if (!dict_obj) {
      return nullptr;
    }
    if (!PyDict_Check(dict_obj)) {
      Py_DECREF(dict_obj);
      PyErr_SetString(PyExc_TypeError, "Expected JSON object for model");
      return nullptr;
    }
    PyObject *instance = PyObject_Call(cls, empty_tuple, dict_obj);
    Py_DECREF(dict_obj);
    if (!instance && outer_collector) {
      // Surface validator errors back to the outer error path.
      PyObject *exc_type = nullptr, *exc_value = nullptr, *exc_tb = nullptr;
      PyErr_Fetch(&exc_type, &exc_value, &exc_tb);
      PyObject *exc_str = exc_value ? PyObject_Str(exc_value) : nullptr;
      const char *msg =
          exc_str ? PyUnicode_AsUTF8(exc_str) : "Nested model failed";
      outer_collector->add_suberror(error_path, msg);
      Py_XDECREF(exc_str);
      Py_XDECREF(exc_type);
      Py_XDECREF(exc_value);
      Py_XDECREF(exc_tb);
      PyErr_Clear();
    }
    return instance;
  }

  PyTypeObject *type = (PyTypeObject *)cls;
  DataModelObject *self = (DataModelObject *)type->tp_alloc(type, 0);
  if (!self) {
    return nullptr;
  }
  self->instance_data = new InstanceData();
  self->instance_data->cached_schema = static_cast<void *>(schema);

  ErrorCollector collector;

  for (Py_ssize_t i = 0; i < schema->num_fields; i++) {
    FieldSchema *fs = &schema->fields[i];
    const rapidjson::Value *member_val = nullptr;

    // Alias lookup.
    if (fs->alias && PyList_Check(fs->alias)) {
      Py_ssize_t n_alias = PyList_Size(fs->alias);
      for (Py_ssize_t j = 0; j < n_alias; j++) {
        PyObject *alias_key = PyList_GetItem(fs->alias, j);
        if (!PyUnicode_Check(alias_key)) {
          continue;
        }
        Py_ssize_t alias_len = 0;
        const char *alias_c =
            PyUnicode_AsUTF8AndSize(alias_key, &alias_len);
        if (!alias_c) {
          PyErr_Clear();
          continue;
        }
        auto m = json_obj.FindMember(rapidjson::Value(
            rapidjson::StringRef(alias_c, alias_len)));
        if (m != json_obj.MemberEnd()) {
          member_val = &m->value;
          break;
        }
      }
    }
    // Canonical field name lookup.
    if (!member_val) {
      auto m = json_obj.FindMember(rapidjson::Value(rapidjson::StringRef(
          fs->field_name_c,
          static_cast<rapidjson::SizeType>(fs->field_name_len))));
      if (m != json_obj.MemberEnd()) {
        member_val = &m->value;
      }
    }

    PyObject *new_value = nullptr;
    if (member_val) {
      new_value = validate_and_convert_from_json(*member_val, fs->type_schema,
                                                 &collector, fs->field_name_c,
                                                 schema->deserializers);
      if (!new_value) {
        // collector already contains the error.
        continue;
      }
    } else {
      // Fall back to defaults.
      if (fs->default_factory != Py_None &&
          PyCallable_Check(fs->default_factory)) {
        new_value = PyObject_CallFunctionObjArgs(fs->default_factory, nullptr);
        if (!new_value) {
          collector.add_error(
              fs->field_name_c,
              "Missing required field and default factory call failed");
          continue;
        }
      } else if (fs->default_value != VLDTUndefined) {
        new_value = fs->default_value;
        Py_INCREF(new_value);
      } else if (fs->type_schema->is_optional) {
        Py_INCREF(Py_None);
        new_value = Py_None;
      } else {
        collector.add_error(fs->field_name_c, "Missing required field");
        continue;
      }
    }
    self->instance_data->fields[fs->field_name_c] = new_value;
  }

  if (collector.has_errors()) {
    if (outer_collector) {
      outer_collector->add_suberror(error_path, collector.to_json());
      Py_DECREF((PyObject *)self);
      return nullptr;
    } else {
      std::string err = collector.to_json();
      PyErr_SetString(PyExc_TypeError, err.c_str());
      Py_DECREF((PyObject *)self);
      return nullptr;
    }
  }

  if (run_field_after_validators(schema, cls, (PyObject *)self) != 0) {
    Py_DECREF((PyObject *)self);
    return nullptr;
  }
  if (run_model_after_validators(schema, cls, (PyObject *)self) != 0) {
    Py_DECREF((PyObject *)self);
    return nullptr;
  }

  return (PyObject *)self;
}

/**
 * @brief Implementation of DataModel_init_from_native declared in
 * data_model.hpp.
 *
 * Populates `self` from `native` directly, bypassing the intermediate Python
 * dict. Callers (currently from_json) must have allocated `self` via
 * tp_alloc + DataModel_new so that instance_data is ready.
 */
extern "C" int DataModel_init_from_native(PyObject *self,
                                          const rapidjson::Value &native) {
  if (!native.IsObject()) {
    PyErr_SetString(PyExc_TypeError, "JSON root must be an object");
    return -1;
  }
  PyObject *cls = (PyObject *)Py_TYPE(self);
  PyObject *built =
      data_model_from_json(cls, native, nullptr, "<root>");
  if (!built) {
    return -1;
  }
  // Move the populated fields from the freshly built instance into `self`.
  DataModelObject *src = (DataModelObject *)built;
  DataModelObject *dst = (DataModelObject *)self;
  // Free anything dst already has.
  for (auto &p : dst->instance_data->fields) {
    Py_XDECREF(p.second);
  }
  dst->instance_data->fields = std::move(src->instance_data->fields);
  dst->instance_data->cached_schema = src->instance_data->cached_schema;
  Py_DECREF(built);
  return 0;
}
