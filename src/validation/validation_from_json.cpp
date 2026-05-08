#include "validation.hpp"

#include "conversion/dict_utils.hpp"
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
#include <vector>

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
 * @brief Forward declaration: primitive leaf path used by container loops.
 */
static inline PyObject *primitive_leaf_from_json(const rapidjson::Value &val,
                                                 TypeSchema *ts);

/**
 * @brief Build the per-element error path "{base}.{index}" lazily.
 */
static void build_indexed_path(char *buf, size_t buf_size, const char *base,
                               size_t base_len, rapidjson::SizeType i) {
  if (base_len >= buf_size - 2) {
    base_len = buf_size - 2;
  }
  std::memcpy(buf, base, base_len);
  buf[base_len] = '.';
  std::snprintf(buf + base_len + 1, buf_size - base_len - 1, "%u", i);
}

/**
 * @brief Validate a JSON array against a list[T] schema.
 *
 * When T is a primitive type (int, float, str, bool, Any) we use a tight
 * inner loop that avoids the per-element function call into
 * validate_and_convert_from_json and avoids building the per-element error
 * path on the success path. The error path is materialized only when an
 * element fails.
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

  TypeSchema *inner = ts->args[0];
  size_t base_len = std::strlen(error_path);

  if (inner->primitive_kind != PK_NONE) {
    // Tight loop for primitive elements. Skip the snprintf on success.
    for (rapidjson::SizeType i = 0; i < size; i++) {
      const rapidjson::Value &v = val[i];
      PyObject *item = primitive_leaf_from_json(v, inner);
      if (!item) {
        // Coercion or null: fall back to the full path with a real error
        // path so any error message points at the right element.
        std::array<char, 256> new_path;
        build_indexed_path(new_path.data(), new_path.size(), error_path,
                           base_len, i);
        item = validate_and_convert_from_json(v, inner, collector,
                                              new_path.data(), deserializers);
        if (!item) {
          Py_DECREF(new_list);
          return nullptr;
        }
      }
      PyList_SET_ITEM(new_list, i, item);
    }
    return new_list;
  }

  // Generic path: full dispatch per element.
  std::array<char, 256> new_path;
  for (rapidjson::SizeType i = 0; i < size; i++) {
    build_indexed_path(new_path.data(), new_path.size(), error_path, base_len,
                       i);
    PyObject *item = validate_and_convert_from_json(
        val[i], inner, collector, new_path.data(), deserializers);
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
 * Keys are always JSON strings. When the key schema is plain str we skip
 * validate_and_convert entirely and use the freshly-built PyUnicode as the
 * key. When the value schema is a primitive we go through the inline leaf
 * path. The error path is materialized only on failure.
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
  bool key_is_str = (key_schema->primitive_kind == PK_STR);
  bool val_is_primitive = (val_schema->primitive_kind != PK_NONE);

  size_t base_len = std::strlen(error_path);
  std::array<char, 256> new_path;

  for (auto itr = val.MemberBegin(); itr != val.MemberEnd(); ++itr) {
    const char *key_str = itr->name.GetString();
    rapidjson::SizeType key_len = itr->name.GetStringLength();

    PyObject *py_key = PyUnicode_FromStringAndSize(key_str, key_len);
    if (!py_key) {
      Py_DECREF(new_dict);
      return nullptr;
    }
    PyObject *conv_key;
    if (key_is_str) {
      conv_key = py_key;
    } else {
      // Build error path lazily for the key validation.
      if (base_len >= new_path.size() - 2) {
        base_len = new_path.size() - 2;
      }
      std::memcpy(new_path.data(), error_path, base_len);
      new_path[base_len] = '.';
      std::snprintf(new_path.data() + base_len + 1,
                    new_path.size() - base_len - 1, "%s", key_str);
      conv_key = validate_and_convert(py_key, key_schema, collector,
                                      new_path.data(), deserializers);
      Py_DECREF(py_key);
      if (!conv_key) {
        Py_DECREF(new_dict);
        return nullptr;
      }
    }

    PyObject *conv_val = nullptr;
    if (val_is_primitive) {
      conv_val = primitive_leaf_from_json(itr->value, val_schema);
    }
    if (!conv_val) {
      // Slow path or coercion: build the error path.
      if (base_len >= new_path.size() - 2) {
        base_len = new_path.size() - 2;
      }
      std::memcpy(new_path.data(), error_path, base_len);
      new_path[base_len] = '.';
      std::snprintf(new_path.data() + base_len + 1,
                    new_path.size() - base_len - 1, "%s", key_str);
      conv_val = validate_and_convert_from_json(
          itr->value, val_schema, collector, new_path.data(), deserializers);
      if (!conv_val) {
        Py_DECREF(conv_key);
        Py_DECREF(new_dict);
        return nullptr;
      }
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
  // Variadic Tuple[T, ...]: any length, every element of args[0].
  if (ts->is_variadic_tuple) {
    PyObject *new_tuple = PyTuple_New(size);
    if (!new_tuple) {
      return nullptr;
    }
    TypeSchema *inner = ts->args[0];
    size_t base_len = std::strlen(error_path);
    std::array<char, 256> new_path;
    for (rapidjson::SizeType i = 0; i < size; i++) {
      PyObject *conv = nullptr;
      if (inner->primitive_kind != PK_NONE) {
        conv = primitive_leaf_from_json(val[i], inner);
      }
      if (!conv) {
        build_indexed_path(new_path.data(), new_path.size(), error_path,
                           base_len, i);
        conv = validate_and_convert_from_json(val[i], inner, collector,
                                              new_path.data(), deserializers);
        if (!conv) {
          Py_DECREF(new_tuple);
          return nullptr;
        }
      }
      PyTuple_SET_ITEM(new_tuple, i, conv);
    }
    return new_tuple;
  }
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
  size_t base_len = std::strlen(error_path);
  std::array<char, 256> new_path;
  for (rapidjson::SizeType i = 0; i < size; i++) {
    TypeSchema *inner = ts->args[i];
    PyObject *conv = nullptr;
    if (inner->primitive_kind != PK_NONE) {
      conv = primitive_leaf_from_json(val[i], inner);
    }
    if (!conv) {
      build_indexed_path(new_path.data(), new_path.size(), error_path,
                         base_len, i);
      conv = validate_and_convert_from_json(val[i], inner, collector,
                                            new_path.data(), deserializers);
      if (!conv) {
        Py_DECREF(new_tuple);
        return nullptr;
      }
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
  TypeSchema *inner = ts->args[0];
  size_t base_len = std::strlen(error_path);
  std::array<char, 256> new_path;
  for (rapidjson::SizeType i = 0; i < val.Size(); i++) {
    PyObject *conv = nullptr;
    if (inner->primitive_kind != PK_NONE) {
      conv = primitive_leaf_from_json(val[i], inner);
    }
    if (!conv) {
      build_indexed_path(new_path.data(), new_path.size(), error_path,
                         base_len, i);
      conv = validate_and_convert_from_json(val[i], inner, collector,
                                            new_path.data(), deserializers);
      if (!conv) {
        Py_DECREF(new_set);
        return nullptr;
      }
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
 * @brief Inline-friendly primitive leaf path.
 *
 * Tries to construct the Python value directly from the rapidjson value,
 * dispatching via the primitive_kind cached on the TypeSchema. Returns
 * nullptr without setting an error if the JSON kind does not match (the
 * caller is expected to fall back to the slow materialize path).
 */
static inline PyObject *
primitive_leaf_from_json(const rapidjson::Value &val, TypeSchema *ts) {
  switch (ts->primitive_kind) {
  case PK_INT:
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
    return nullptr;
  case PK_FLOAT:
    if (val.IsDouble() || val.IsLosslessDouble()) {
      return PyFloat_FromDouble(val.GetDouble());
    }
    if (val.IsInt()) {
      return PyFloat_FromDouble(static_cast<double>(val.GetInt()));
    }
    if (val.IsInt64()) {
      return PyFloat_FromDouble(static_cast<double>(val.GetInt64()));
    }
    return nullptr;
  case PK_STR:
    if (val.IsString()) {
      return PyUnicode_FromStringAndSize(val.GetString(),
                                         val.GetStringLength());
    }
    return nullptr;
  case PK_BOOL:
    if (val.IsBool()) {
      if (val.GetBool()) {
        Py_RETURN_TRUE;
      }
      Py_RETURN_FALSE;
    }
    return nullptr;
  case PK_ANY:
    return materialize(val);
  default:
    return nullptr;
  }
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
    if (ts->is_optional || ts->primitive_kind == PK_ANY) {
      Py_INCREF(Py_None);
      return Py_None;
    }
    // Fall through; non-Optional fields will report a type mismatch via the
    // materialized fallback below.
  }

  // typing.Literal[...] needs value-equality against the literal tuple.
  // Materialise the JSON leaf as a Python value and delegate to
  // validate_and_convert which holds the Literal logic in one place.
  if (ts->is_literal) {
    PyObject *as_py = materialize(val);
    if (!as_py) {
      return nullptr;
    }
    PyObject *result =
        validate_and_convert(as_py, ts, collector, error_path, deserializers);
    Py_DECREF(as_py);
    return result;
  }

  // Primitive fast paths via the cached primitive_kind.
  if (ts->primitive_kind != PK_NONE) {
    PyObject *prim = primitive_leaf_from_json(val, ts);
    if (prim) {
      return prim;
    }
    // Not a matching primitive kind; fall through to coercion.
  } else if (ts->is_data_model && val.IsObject()) {
    return data_model_from_json(ts->expected_type, val, collector, error_path);
  } else {
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
 * @brief Build a per-field array of pointers into the JSON object.
 *
 * Walks the JSON object once and uses SchemaCache::name_index (a hashmap
 * keyed by canonical name plus all aliases) to dispatch each member to
 * its target field index. The hashmap is built once at schema compile
 * time, so this routine is O(M) per record where M is the number of
 * JSON members, regardless of model width.
 *
 * @param json_obj   The rapidjson object to walk.
 * @param schema     The compiled schema for the model.
 * @param member_out Caller-provided buffer of size schema->num_fields.
 *                   Initialized to nullptr by this function.
 */
static void
build_member_lookup(const rapidjson::Value &json_obj, SchemaCache *schema,
                    const rapidjson::Value **member_out) {
  Py_ssize_t n = schema->num_fields;
  for (Py_ssize_t i = 0; i < n; i++) {
    member_out[i] = nullptr;
  }
  for (auto itr = json_obj.MemberBegin(); itr != json_obj.MemberEnd(); ++itr) {
    const char *key_c = itr->name.GetString();
    rapidjson::SizeType key_len = itr->name.GetStringLength();
    auto it = schema->name_index.find(
        std::string_view(key_c, static_cast<size_t>(key_len)));
    if (it == schema->name_index.end()) {
      continue;
    }
    Py_ssize_t idx = it->second;
    // First write wins: if both the canonical name and an alias appear in
    // the JSON object, keep the value found first.
    if (member_out[idx] == nullptr) {
      member_out[idx] = &itr->value;
    }
  }
}

/**
 * @brief Look up a field in a Python dict by canonical name then aliases.
 *
 * Returns a borrowed reference to the matching value, or nullptr if absent.
 */
static PyObject *lookup_member_in_dict(PyObject *kwds, FieldSchema *fs) {
  if (fs->alias && PyList_Check(fs->alias)) {
    Py_ssize_t n_alias = PyList_Size(fs->alias);
    for (Py_ssize_t j = 0; j < n_alias; j++) {
      PyObject *alias_key = PyList_GetItem(fs->alias, j);
      PyObject *v = PyDict_GetItem(kwds, alias_key);
      if (v) {
        return v;
      }
    }
  }
  return PyDict_GetItem(kwds, fs->field_name);
}

/**
 * @brief Resolve the default for a field that was not present in the input.
 *
 * Returns a new reference to the default value, or nullptr on missing-field
 * (recording an error in collector) or when the default factory raised.
 */
static PyObject *resolve_default(FieldSchema *fs, ErrorCollector *collector) {
  if (fs->default_factory != Py_None &&
      PyCallable_Check(fs->default_factory)) {
    PyObject *v = PyObject_CallFunctionObjArgs(fs->default_factory, nullptr);
    if (!v) {
      if (collector) {
        collector->add_error(
            fs->field_name_c,
            "Missing required field and default factory call failed");
      }
      PyErr_Clear();
    }
    return v;
  }
  if (fs->default_value != VLDTUndefined) {
    Py_INCREF(fs->default_value);
    return fs->default_value;
  }
  if (fs->type_schema->is_optional) {
    Py_INCREF(Py_None);
    return Py_None;
  }
  if (collector) {
    collector->add_error(fs->field_name_c, "Missing required field");
  }
  return nullptr;
}

/**
 * @brief Construct a DataModel directly from a rapidjson object.
 *
 * Allocates a fresh instance of `cls`, populates instance_data->fields by
 * walking the schema fields once, and runs every validator hook (model_before,
 * field_before, field_after, model_after) along the way.
 *
 * Three observations make the one-pass walk possible without giving up
 * validator semantics:
 *
 *   - model_before takes a dict, so when it is present we materialize the
 *     dict once, run the validator, then drive the schema walk from the
 *     (possibly modified) dict instead of the rapidjson DOM. The rest of
 *     the work is still single-pass.
 *   - field_before takes a single Python value, so we only materialize the
 *     specific field's rapidjson value when that field has a field_before
 *     validator. Other fields take the direct rapidjson path and avoid the
 *     extra allocation.
 *   - field_after and model_after run on the constructed instance, so they
 *     fit into the same flow regardless of where the field values came from.
 *
 * The only structural fallback is for subclasses that override __init__ in
 * Python (notably AsyncDataModel, which stashes kwargs and runs validation
 * on await). Those classes need cls.__call__ to dispatch through the Python
 * override, so we materialize a dict and call cls(**dict).
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

  // AsyncDataModel and any user subclass that overrides __init__ need the
  // Python-level dispatch. tp_init is replaced by slot_tp_init in that case.
  PyTypeObject *cls_type = (PyTypeObject *)cls;
  if (cls_type->tp_init != DataModel_init) {
    PyObject *dict_obj = rapidjson_to_pyobject(json_obj);
    if (!dict_obj) {
      return nullptr;
    }
    if (!PyDict_Check(dict_obj)) {
      Py_DECREF(dict_obj);
      PyErr_SetString(PyExc_TypeError, "Expected JSON object for model");
      return nullptr;
    }
    if (!validate_dict_keys_are_unicode(dict_obj)) {
      Py_DECREF(dict_obj);
      return nullptr;
    }
    PyObject *instance = PyObject_Call(cls, empty_tuple, dict_obj);
    Py_DECREF(dict_obj);
    if (!instance && outer_collector) {
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

  // model_before runs against a dict. Build one only if needed.
  PyObject *kwds = nullptr;
  if (schema->has_model_before) {
    kwds = rapidjson_to_pyobject(json_obj);
    if (!kwds) {
      return nullptr;
    }
    if (!PyDict_Check(kwds)) {
      Py_DECREF(kwds);
      PyErr_SetString(PyExc_TypeError, "Expected JSON object for model");
      return nullptr;
    }
    if (run_model_before_validators(schema, cls, &kwds) != 0) {
      Py_DECREF(kwds);
      return nullptr;
    }
    if (!validate_dict_keys_are_unicode(kwds)) {
      Py_DECREF(kwds);
      return nullptr;
    }
  }

  // Pre-fetch the field_before dict once so per-field lookups stay cheap.
  PyObject *field_before_dict = nullptr;
  if (schema->has_field_before && schema->validators &&
      PyDict_Check(schema->validators)) {
    field_before_dict =
        PyDict_GetItemString(schema->validators, "field_before");
    if (field_before_dict && !PyDict_Check(field_before_dict)) {
      field_before_dict = nullptr;
    }
  }

  DataModelObject *self =
      (DataModelObject *)cls_type->tp_alloc(cls_type, 0);
  if (!self) {
    Py_XDECREF(kwds);
    return nullptr;
  }
  self->instance_data = new InstanceData();
  self->instance_data->cached_schema = static_cast<void *>(schema);
  self->instance_data->dict_initialized = false;
  self->instance_data->fields.assign(schema->num_fields, nullptr);

  ErrorCollector collector;

  // Pre-compute a JSON member pointer per schema field in one pass over the
  // JSON object. This replaces N rapidjson::FindMember calls (each O(M)) with
  // a single walk that matches as it goes. Skipped when a model_before
  // validator already produced the canonical kwargs dict.
  constexpr size_t STACK_BUF = 64;
  const rapidjson::Value *stack_members[STACK_BUF];
  std::vector<const rapidjson::Value *> heap_members;
  const rapidjson::Value **member_for_field = nullptr;
  if (!kwds) {
    if (schema->num_fields <= static_cast<Py_ssize_t>(STACK_BUF)) {
      member_for_field = stack_members;
    } else {
      heap_members.assign(schema->num_fields, nullptr);
      member_for_field = heap_members.data();
    }
    build_member_lookup(json_obj, schema, member_for_field);
  }

  for (Py_ssize_t i = 0; i < schema->num_fields; i++) {
    FieldSchema *fs = &schema->fields[i];

    // Per-field field_before validators (may be absent for this field).
    PyObject *fb_validators = nullptr;
    if (field_before_dict) {
      fb_validators = PyDict_GetItem(field_before_dict, fs->field_name);
      if (fb_validators && !PyList_Check(fb_validators)) {
        fb_validators = nullptr;
      }
    }

    PyObject *new_value = nullptr;
    if (kwds) {
      // model_before path: drive from the (possibly modified) dict.
      PyObject *raw = lookup_member_in_dict(kwds, fs);
      PyObject *value = nullptr;
      if (raw) {
        Py_INCREF(raw);
        value = raw;
      } else {
        value = resolve_default(fs, &collector);
        if (!value) {
          continue;
        }
      }
      if (fb_validators) {
        value = run_field_validators_on_value(cls, fb_validators, value);
        if (!value) {
          // Validator raised. Propagate via the existing C exception.
          Py_DECREF((PyObject *)self);
          Py_DECREF(kwds);
          return nullptr;
        }
      }
      new_value = validate_and_convert(value, fs->type_schema, &collector,
                                       fs->field_name_c, schema->deserializers);
      Py_DECREF(value);
    } else {
      // No model_before: use the precomputed lookup.
      const rapidjson::Value *member = member_for_field[i];
      if (!member) {
        PyObject *value = resolve_default(fs, &collector);
        if (!value) {
          continue;
        }
        if (fb_validators) {
          value = run_field_validators_on_value(cls, fb_validators, value);
          if (!value) {
            Py_DECREF((PyObject *)self);
            return nullptr;
          }
        }
        new_value = validate_and_convert(value, fs->type_schema, &collector,
                                         fs->field_name_c,
                                         schema->deserializers);
        Py_DECREF(value);
      } else if (fb_validators) {
        // field_before requires a Python value: materialize this leaf only.
        PyObject *value = materialize(*member);
        if (!value) {
          Py_DECREF((PyObject *)self);
          return nullptr;
        }
        value = run_field_validators_on_value(cls, fb_validators, value);
        if (!value) {
          Py_DECREF((PyObject *)self);
          return nullptr;
        }
        new_value = validate_and_convert(value, fs->type_schema, &collector,
                                         fs->field_name_c,
                                         schema->deserializers);
        Py_DECREF(value);
      } else {
        // Direct rapidjson path: no allocation for the dict, no allocation
        // for the field value beyond the validated result itself.
        new_value = validate_and_convert_from_json(*member, fs->type_schema,
                                                   &collector,
                                                   fs->field_name_c,
                                                   schema->deserializers);
      }
    }
    if (!new_value) {
      // collector already contains the error.
      continue;
    }
    Py_XDECREF(self->instance_data->fields[i]);
    self->instance_data->fields[i] = new_value;
  }

  // Apply the extra-fields policy. For the model_before path the source
  // has already been merged into kwds (a Python dict); for the direct
  // rapidjson path we walk the JSON members.
  if (schema->extra_policy != EXTRA_IGNORE) {
    auto handle_extra = [&](const char *key_c, Py_ssize_t key_len,
                            PyObject *py_val_or_null,
                            const rapidjson::Value *json_val_or_null) {
      auto it = schema->name_index.find(
          std::string_view(key_c, static_cast<size_t>(key_len)));
      if (it != schema->name_index.end()) {
        return true;
      }
      if (schema->extra_policy == EXTRA_FORBID) {
        collector.add_error(key_c, "Unexpected field");
      } else if (schema->extra_policy == EXTRA_ALLOW) {
        PyObject *value = py_val_or_null;
        if (!value && json_val_or_null) {
          value = rapidjson_to_pyobject(*json_val_or_null);
          if (!value) {
            return false;
          }
        } else if (!value) {
          return true;
        } else {
          Py_INCREF(value);
        }
        if (!self->instance_data->extra_fields) {
          self->instance_data->extra_fields =
              std::make_unique<ExtraFieldsMap>();
        }
        auto &m = *self->instance_data->extra_fields;
        auto exi = m.find(
            std::string_view(key_c, static_cast<size_t>(key_len)));
        if (exi != m.end()) {
          Py_XDECREF(exi->second);
          exi->second = value;
        } else {
          m.emplace(std::string(key_c, static_cast<size_t>(key_len)), value);
        }
      }
      return true;
    };
    if (kwds) {
      PyObject *xkey;
      PyObject *xvalue;
      Py_ssize_t xpos = 0;
      while (PyDict_Next(kwds, &xpos, &xkey, &xvalue)) {
        if (!PyUnicode_Check(xkey)) {
          continue;
        }
        Py_ssize_t klen = 0;
        const char *kc = PyUnicode_AsUTF8AndSize(xkey, &klen);
        if (!kc) {
          PyErr_Clear();
          continue;
        }
        if (!handle_extra(kc, klen, xvalue, nullptr)) {
          Py_DECREF((PyObject *)self);
          Py_XDECREF(kwds);
          return nullptr;
        }
      }
    } else {
      for (auto itr = json_obj.MemberBegin(); itr != json_obj.MemberEnd();
           ++itr) {
        const char *kc = itr->name.GetString();
        Py_ssize_t klen = static_cast<Py_ssize_t>(itr->name.GetStringLength());
        if (!handle_extra(kc, klen, nullptr, &itr->value)) {
          Py_DECREF((PyObject *)self);
          return nullptr;
        }
      }
    }
  }

  Py_XDECREF(kwds);

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
  for (PyObject *v : dst->instance_data->fields) {
    Py_XDECREF(v);
  }
  if (dst->instance_data->extra_fields) {
    for (auto &p : *dst->instance_data->extra_fields) {
      Py_XDECREF(p.second);
    }
    dst->instance_data->extra_fields.reset();
  }
  dst->instance_data->fields = std::move(src->instance_data->fields);
  dst->instance_data->extra_fields = std::move(src->instance_data->extra_fields);
  dst->instance_data->cached_schema = src->instance_data->cached_schema;
  Py_DECREF(built);
  return 0;
}
