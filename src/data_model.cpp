#include <Python.h>
#include <functional>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "conversion/dict_utils.hpp"
#include "conversion/json_utils.hpp"
#include "conversion/rapidjson_to_pyobject.hpp"
#include "data_model.hpp"
#include "error_handling.hpp"
#include "init_globals.hpp"
#include "schema/schema.hpp"
#include "validation/validation.hpp"
#include "validation/validation_validators.hpp"

PyObject *schema_key = nullptr;
static PyObject *field_prefix = nullptr;
static PyObject *field_suffix = nullptr;
PyObject *FieldType = nullptr;
PyObject *FieldUndefined = nullptr;
static PyObject *default_str = nullptr;
static PyObject *default_factory_str = nullptr;
// copy.deepcopy resolved once at module init so __deepcopy__ can defer to
// it for built-in containers (list/dict/set) without per-call import cost.
static PyObject *CopyDeepcopyFunc = nullptr;
static PyObject *CopyCopyFunc = nullptr;

/**
 * @brief Initialize globals for DataModel.
 *
 * @return int 0 on success, -1 on failure.
 */
int init_data_model_globals(void) {
  schema_key = PyUnicode_InternFromString("__vldt_schema__");
  field_prefix = PyUnicode_InternFromString("Field '");
  field_suffix = PyUnicode_InternFromString("': ");

  if (init_extension_globals() != 0) {
    return -1;
  }

  PyObject *fields_module = PyImport_ImportModule("vldt.fields");
  if (!fields_module) {
    return -1;
  }
  FieldType = PyObject_GetAttrString(fields_module, "Field");
  if (!FieldType) {
    Py_DECREF(fields_module);
    return -1;
  }
  FieldUndefined = PyObject_GetAttrString(fields_module, "UNDEFINED");
  Py_DECREF(fields_module);
  if (!FieldUndefined) {
    return -1;
  }

  default_str = PyUnicode_InternFromString("default");
  default_factory_str = PyUnicode_InternFromString("default_factory");

  PyObject *copy_module = PyImport_ImportModule("copy");
  if (!copy_module) {
    return -1;
  }
  CopyDeepcopyFunc = PyObject_GetAttrString(copy_module, "deepcopy");
  CopyCopyFunc = PyObject_GetAttrString(copy_module, "copy");
  Py_DECREF(copy_module);
  if (!CopyDeepcopyFunc || !CopyCopyFunc) {
    Py_XDECREF(CopyDeepcopyFunc);
    Py_XDECREF(CopyCopyFunc);
    return -1;
  }

  return 0;
}

/**
 * @brief Check if expected_type is a ClassVar.
 *
 * @param expected_type Python object representing the expected type.
 * @return int 1 if ClassVar, 0 otherwise.
 */
static int is_class_var(PyObject *expected_type) {
  PyObject *origin = PyObject_GetAttrString(expected_type, "__origin__");
  if (origin) {
    int result = (origin == ClassVarType);
    Py_DECREF(origin);
    return result;
  }
  PyErr_Clear();
  return 0;
}

/**
 * @brief DataModel.__new__ implementation.
 *
 * @param type Python type.
 * @param args Arguments.
 * @param kwds Keyword arguments.
 * @return PyObject* New instance.
 */
PyObject *DataModel_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
  DataModelObject *self = (DataModelObject *)type->tp_alloc(type, 0);
  if (self) {
    self->instance_data = new InstanceData();
    self->instance_data->cached_schema = nullptr;
    self->instance_data->dict_initialized = false;
  }
  return (PyObject *)self;
}

/**
 * @brief DataModel.__dealloc__ implementation.
 *
 * @param self Python object.
 */
void DataModel_dealloc(PyObject *self) {
  DataModelObject *bm_self = (DataModelObject *)self;
  for (PyObject *v : bm_self->instance_data->fields) {
    Py_XDECREF(v);
  }
  if (bm_self->instance_data->extra_fields) {
    for (auto &p : *bm_self->instance_data->extra_fields) {
      Py_XDECREF(p.second);
    }
  }
  delete bm_self->instance_data;
  Py_TYPE(self)->tp_free(self);
}

/**
 * @brief DataModel.__getattro__ implementation.
 *
 * Resolves declared fields through SchemaCache::name_index in O(1), reading
 * the value out of the instance's flat fields vector. Falls back to the
 * lazily-allocated extra_fields map for arbitrary attributes set by user
 * code, then to PyObject_GenericGetAttr for class-level attributes (methods,
 * descriptors, etc).
 *
 * @param self Python object.
 * @param name Attribute name.
 * @return PyObject* Attribute value (new reference) or nullptr on error.
 */
PyObject *DataModel_getattro(PyObject *self, PyObject *name) {
  DataModelObject *bm_self = (DataModelObject *)self;
  InstanceData *data = bm_self->instance_data;

  Py_ssize_t name_len = 0;
  const char *attr_name = PyUnicode_AsUTF8AndSize(name, &name_len);
  if (!attr_name) {
    return nullptr;
  }

  SchemaCache *schema = static_cast<SchemaCache *>(data->cached_schema);
  if (schema) {
    auto it = schema->name_index.find(
        std::string_view(attr_name, static_cast<size_t>(name_len)));
    if (it != schema->name_index.end()) {
      Py_ssize_t idx = it->second;
      if (idx < static_cast<Py_ssize_t>(data->fields.size())) {
        PyObject *v = data->fields[idx];
        if (v) {
          Py_INCREF(v);
          return v;
        }
      }
    }
  }
  if (data->extra_fields) {
    auto it = data->extra_fields->find(
        std::string_view(attr_name, static_cast<size_t>(name_len)));
    if (it != data->extra_fields->end()) {
      Py_INCREF(it->second);
      return it->second;
    }
  }
  return PyObject_GenericGetAttr(self, name);
}

/**
 * @brief DataModel.__init__ implementation.
 *
 * This merged implementation:
 *   - Allocates the instance data.
 *   - Retrieves and validates the schema.
 *   - Runs BEFORE validators.
 *   - Iterates over each field to extract values directly from kwds (checking
 *     aliases first), falling back to default_factory, default_value, or None
 * as needed.
 *   - Validates and converts each field value.
 *   - Runs AFTER validators.
 *
 * @param self Python object.
 * @param args Positional arguments (none allowed).
 * @param kwds Keyword arguments.
 * @return int 0 on success, -1 on failure.
 */
int DataModel_init(PyObject *self, PyObject *args, PyObject *kwds) {
  DataModelObject *bm_self = (DataModelObject *)self;

  PyObject *cls = (PyObject *)Py_TYPE(self);
  PyObject *capsule = get_schema_cached(cls);
  if (!capsule) {
    PyErr_SetString(PyExc_TypeError, "Could not compile model schema");
    return -1;
  }
  SchemaCache *schema =
      (SchemaCache *)PyCapsule_GetPointer(capsule, "vldt.SchemaCache");
  Py_DECREF(capsule);
  bm_self->instance_data->cached_schema = static_cast<void *>(schema);
  if (!schema) {
    PyErr_SetString(PyExc_TypeError, "Schema capsule is invalid");
    return -1;
  }

  if (run_model_before_validators(schema, cls, &kwds) != 0) {
    return -1;
  }
  if (run_field_before_validators(schema, cls, &kwds) != 0) {
    return -1;
  }

  InstanceData *data = bm_self->instance_data;
  data->fields.assign(schema->num_fields, nullptr);
  ErrorCollector collector;
  for (Py_ssize_t i = 0; i < schema->num_fields; i++) {
    FieldSchema *fs = &schema->fields[i];
    const char *field_path = fs->field_name_c;
    PyObject *value = nullptr;

    if (kwds && PyDict_Check(kwds)) {
      if (fs->alias && PyList_Check(fs->alias)) {
        Py_ssize_t n_alias = PyList_Size(fs->alias);
        for (Py_ssize_t j = 0; j < n_alias; j++) {
          PyObject *alias_key = PyList_GetItem(fs->alias, j);
          value = PyDict_GetItem(kwds, alias_key);
          if (value) {
            Py_INCREF(value);
            break;
          }
        }
      }
      if (!value) {
        value = PyDict_GetItem(kwds, fs->field_name);
        if (value) {
          Py_INCREF(value);
        }
      }
    }

    if (!value) {
      if (fs->default_factory != Py_None &&
          PyCallable_Check(fs->default_factory)) {
        value = PyObject_CallFunctionObjArgs(fs->default_factory, nullptr);
        if (!value) {
          collector.add_error(
              field_path,
              "Missing required field and default factory call failed");
          continue;
        }
      } else if (fs->default_value != VLDTUndefined) {
        value = fs->default_value;
        Py_INCREF(value);
      } else if (fs->type_schema->is_optional) {
        Py_INCREF(Py_None);
        value = Py_None;
      } else {
        collector.add_error(field_path, "Missing required field");
        continue;
      }
    }

    PyObject *new_value = validate_and_convert(
        value, fs->type_schema, &collector, field_path, schema->deserializers);
    if (!new_value) {
      // Keep the raw value in the slot so post-error inspection still works
      // (the error has already been recorded into the collector).
      Py_XDECREF(data->fields[i]);
      Py_INCREF(value);
      data->fields[i] = value;
      Py_DECREF(value);
      continue;
    }
    Py_DECREF(value);
    Py_XDECREF(data->fields[i]);
    data->fields[i] = new_value;
  }

  if (collector.has_errors()) {
    std::string err_json = collector.to_json();
    PyErr_SetString(PyExc_TypeError, err_json.c_str());
    return -1;
  }

  if (run_field_after_validators(schema, cls, self) != 0) {
    return -1;
  }
  if (run_model_after_validators(schema, cls, self) != 0) {
    return -1;
  }
  return 0;
}

/**
 * @brief DataModel.__setattro__ implementation.
 *
 * @param self Python object.
 * @param name Attribute name.
 * @param value Attribute value.
 * @return int 0 on success, -1 on failure.
 */
int DataModel_setattro(PyObject *self, PyObject *name, PyObject *value) {
  DataModelObject *bm_self = (DataModelObject *)self;
  InstanceData *data = bm_self->instance_data;

  if (!PyUnicode_Check(name)) {
    return PyObject_GenericSetAttr(self, name, value);
  }
  Py_ssize_t name_len = 0;
  const char *attr_name = PyUnicode_AsUTF8AndSize(name, &name_len);
  if (!attr_name) {
    return -1;
  }
  std::string_view name_view(attr_name, static_cast<size_t>(name_len));

  PyObject *capsule = get_schema_cached((PyObject *)Py_TYPE(self));
  if (!capsule) {
    PyErr_SetString(PyExc_TypeError, "Could not retrieve model schema");
    return -1;
  }
  SchemaCache *schema =
      (SchemaCache *)PyCapsule_GetPointer(capsule, "vldt.SchemaCache");
  Py_DECREF(capsule);
  if (!schema) {
    return -1;
  }

  PyObject *annotations = schema->instance_annotations;

  auto it = schema->name_index.find(name_view);
  TypeSchema *ts = nullptr;
  Py_ssize_t idx = -1;
  if (it != schema->name_index.end()) {
    idx = it->second;
    if (idx < 0 || idx >= schema->num_fields) {
      PyErr_Format(PyExc_RuntimeError,
                   "Invalid field index %zd for attribute %R", idx, name);
      return -1;
    }
    ts = schema->fields[idx].type_schema;
    if (!ts) {
      PyErr_SetString(PyExc_RuntimeError,
                      "Missing compiled field schema for attribute");
      return -1;
    }
  } else if (annotations && PyDict_Check(annotations) &&
             PyDict_Contains(annotations, name)) {
    PyObject *expected_type = PyDict_GetItemWithError(annotations, name);
    if (!expected_type) {
      return -1;
    }
    if (is_class_var(expected_type)) {
      PyErr_SetString(PyExc_AttributeError, "Cannot set ClassVar attribute");
      return -1;
    }
    ts = compile_type_schema(expected_type);
    if (!ts) {
      return -1;
    }
  }

  if (ts) {
    ErrorCollector collector;
    PyObject *converted = validate_and_convert(
        value, ts, &collector, attr_name, schema->deserializers);
    if (it == schema->name_index.end()) {
      free_type_schema(ts);
    }
    if (!converted) {
      if (collector.has_errors()) {
        std::string err_json = collector.to_json();
        PyErr_SetString(PyExc_TypeError, err_json.c_str());
      } else {
        PyErr_Format(PyExc_TypeError, "Invalid value for attribute %R", name);
      }
      return -1;
    }
    if (it == schema->name_index.end()) {
      // Annotation present but not in the index (shouldn't happen for normal
      // models, but stay defensive). Fall through to the extra map.
      if (!data->extra_fields) {
        data->extra_fields = std::make_unique<ExtraFieldsMap>();
      }
      auto &m = *data->extra_fields;
      auto exi = m.find(name_view);
      if (exi != m.end()) {
        Py_XDECREF(exi->second);
        exi->second = converted;
      } else {
        m.emplace(std::string(attr_name, static_cast<size_t>(name_len)),
                  converted);
      }
      return 0;
    }
    if (idx >= static_cast<Py_ssize_t>(data->fields.size())) {
      data->fields.resize(static_cast<size_t>(schema->num_fields), nullptr);
    }
    Py_XDECREF(data->fields[idx]);
    data->fields[idx] = converted;
    return 0;
  }

  // Non-schema attribute: park it in the lazy extras map.
  if (!data->extra_fields) {
    data->extra_fields = std::make_unique<ExtraFieldsMap>();
  }
  auto &m = *data->extra_fields;
  Py_INCREF(value);
  auto exi = m.find(name_view);
  if (exi != m.end()) {
    Py_XDECREF(exi->second);
    exi->second = value;
  } else {
    m.emplace(std::string(attr_name, static_cast<size_t>(name_len)), value);
  }
  return 0;
}

/**
 * @brief DataModel.__deepcopy__ implementation.
 *
 * @param self Python object.
 * @param args Arguments.
 * @return PyObject* Deep copied object.
 */
static PyObject *deepcopy_one(PyObject *value, PyObject *memo) {
  // Defer to copy.deepcopy so containers (list, dict, set) and arbitrary
  // user types are deep-copied correctly. The previous shortcut that just
  // INCREF'd values lacking __deepcopy__ silently shared mutable state.
  return PyObject_CallFunctionObjArgs(CopyDeepcopyFunc, value, memo, nullptr);
}

static PyObject *shallowcopy_one(PyObject *value) {
  // copy.copy produces a shallow copy for containers and user types.
  return PyObject_CallFunctionObjArgs(CopyCopyFunc, value, nullptr);
}

static PyObject *DataModel_copy(PyObject *self, PyObject *Py_UNUSED(ignored)) {
  PyTypeObject *type = Py_TYPE(self);
  PyObject *new_obj = type->tp_alloc(type, 0);
  if (!new_obj) {
    return nullptr;
  }

  DataModelObject *src = (DataModelObject *)self;
  DataModelObject *dst = (DataModelObject *)new_obj;
  dst->instance_data = new InstanceData();
  SchemaCache *schema =
      static_cast<SchemaCache *>(src->instance_data->cached_schema);
  dst->instance_data->cached_schema = schema;
  dst->instance_data->dict_initialized = false;

  ErrorCollector collector;
  Py_ssize_t n_src = static_cast<Py_ssize_t>(src->instance_data->fields.size());
  dst->instance_data->fields.resize(static_cast<size_t>(n_src), nullptr);
  for (Py_ssize_t i = 0; i < n_src; i++) {
    PyObject *v = src->instance_data->fields[i];
    if (!v) {
      continue;
    }
    PyObject *copied = shallowcopy_one(v);
    if (!copied) {
      Py_DECREF(new_obj);
      return nullptr;
    }
    if (schema && i < schema->num_fields) {
      FieldSchema *fs = &schema->fields[i];
      PyObject *validated = validate_and_convert(
          copied, fs->type_schema, &collector, fs->field_name_c,
          schema->deserializers);
      Py_DECREF(copied);
      if (!validated) {
        std::string err = collector.to_json();
        PyErr_SetString(PyExc_TypeError, err.c_str());
        Py_DECREF(new_obj);
        return nullptr;
      }
      dst->instance_data->fields[i] = validated;
    } else {
      dst->instance_data->fields[i] = copied;
    }
  }

  if (src->instance_data->extra_fields) {
    dst->instance_data->extra_fields = std::make_unique<ExtraFieldsMap>();
    for (const auto &p : *src->instance_data->extra_fields) {
      PyObject *copied = shallowcopy_one(p.second);
      if (!copied) {
        Py_DECREF(new_obj);
        return nullptr;
      }
      dst->instance_data->extra_fields->emplace(p.first, copied);
    }
  }
  return new_obj;
}

static PyObject *DataModel_deepcopy(PyObject *self, PyObject *args) {
  PyObject *memo;
  if (!PyArg_ParseTuple(args, "O", &memo)) {
    return nullptr;
  }

  PyTypeObject *type = Py_TYPE(self);
  PyObject *new_obj = type->tp_alloc(type, 0);
  if (!new_obj) {
    return nullptr;
  }

  DataModelObject *src = (DataModelObject *)self;
  DataModelObject *dst = (DataModelObject *)new_obj;
  dst->instance_data = new InstanceData();
  SchemaCache *schema =
      static_cast<SchemaCache *>(src->instance_data->cached_schema);
  dst->instance_data->cached_schema = schema;
  dst->instance_data->dict_initialized = false;

  // Each field is deep-copied via copy.deepcopy and then re-validated
  // against its schema. This catches type-invariant violations introduced
  // by direct mutation of mutable contents (e.g. list field whose contents
  // were appended-to with a wrong-typed value), instead of silently
  // propagating the broken state into the copy.
  ErrorCollector collector;
  Py_ssize_t n_src = static_cast<Py_ssize_t>(src->instance_data->fields.size());
  dst->instance_data->fields.resize(static_cast<size_t>(n_src), nullptr);
  for (Py_ssize_t i = 0; i < n_src; i++) {
    PyObject *v = src->instance_data->fields[i];
    if (!v) {
      continue;
    }
    PyObject *copied = deepcopy_one(v, memo);
    if (!copied) {
      Py_DECREF(new_obj);
      return nullptr;
    }
    if (schema && i < schema->num_fields) {
      FieldSchema *fs = &schema->fields[i];
      PyObject *validated = validate_and_convert(
          copied, fs->type_schema, &collector, fs->field_name_c,
          schema->deserializers);
      Py_DECREF(copied);
      if (!validated) {
        // collector has the error; surface it now and bail.
        std::string err = collector.to_json();
        PyErr_SetString(PyExc_TypeError, err.c_str());
        Py_DECREF(new_obj);
        return nullptr;
      }
      dst->instance_data->fields[i] = validated;
    } else {
      dst->instance_data->fields[i] = copied;
    }
  }

  if (src->instance_data->extra_fields) {
    dst->instance_data->extra_fields = std::make_unique<ExtraFieldsMap>();
    for (const auto &p : *src->instance_data->extra_fields) {
      PyObject *copied = deepcopy_one(p.second, memo);
      if (!copied) {
        Py_DECREF(new_obj);
        return nullptr;
      }
      dst->instance_data->extra_fields->emplace(p.first, copied);
    }
  }
  return new_obj;
}

static PyMethodDef DataModel_methods[] = {
    {"from_dict", (PyCFunction)dict_utils_from_dict, METH_CLASS | METH_VARARGS,
     "Create an instance from a dictionary."},
    {"to_dict", (PyCFunction)dict_utils_to_dict, METH_NOARGS,
     "Convert the model instance to a dictionary."},
    {"from_json", (PyCFunction)json_utils_from_json, METH_CLASS | METH_FASTCALL,
     "Create an instance from a JSON string."},
    {"to_json", (PyCFunction)json_utils_to_json, METH_NOARGS,
     "Convert the model instance to a JSON string."},
    {"__copy__", (PyCFunction)DataModel_copy, METH_NOARGS,
     "Shallow copy the model instance."},
    {"__deepcopy__", (PyCFunction)DataModel_deepcopy, METH_VARARGS,
     "Deep copy the model instance."},
    {nullptr, nullptr, 0, nullptr}};

PyTypeObject DataModelType = {
    .ob_base = {.ob_base = {.ob_refcnt = 1, .ob_type = &PyType_Type},
                .ob_size = 0},
    .tp_name = "vldt._vldt.DataModel",
    .tp_basicsize = sizeof(DataModelObject),
    .tp_itemsize = 0,
    .tp_dealloc = DataModel_dealloc,
    .tp_vectorcall_offset = 0,
    .tp_getattr = nullptr,
    .tp_setattr = nullptr,
    .tp_as_async = nullptr,
    .tp_repr = nullptr,
    .tp_as_number = nullptr,
    .tp_as_sequence = nullptr,
    .tp_as_mapping = nullptr,
    .tp_hash = nullptr,
    .tp_call = nullptr,
    .tp_str = nullptr,
    .tp_getattro = DataModel_getattro,
    .tp_setattro = DataModel_setattro,
    .tp_as_buffer = nullptr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "DataModel objects",
    .tp_traverse = nullptr,
    .tp_clear = nullptr,
    .tp_richcompare = nullptr,
    .tp_weaklistoffset = 0,
    .tp_iter = nullptr,
    .tp_iternext = nullptr,
    .tp_methods = DataModel_methods,
    .tp_members = nullptr,
    .tp_getset = nullptr,
    .tp_base = nullptr,
    .tp_dict = nullptr,
    .tp_descr_get = nullptr,
    .tp_descr_set = nullptr,
    .tp_dictoffset = 0,
    .tp_init = DataModel_init,
    .tp_alloc = nullptr,
    .tp_new = DataModel_new,
    .tp_free = nullptr,
    .tp_is_gc = nullptr,
    .tp_bases = nullptr,
    .tp_mro = nullptr,
    .tp_cache = nullptr,
    .tp_subclasses = nullptr,
    .tp_weaklist = nullptr,
    .tp_del = nullptr,
    .tp_version_tag = 0,
    .tp_finalize = nullptr};
