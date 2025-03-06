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
static PyObject *default_str = nullptr;
static PyObject *default_factory_str = nullptr;

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
  Py_DECREF(fields_module);
  if (!FieldType) {
    return -1;
  }

  default_str = PyUnicode_InternFromString("default");
  default_factory_str = PyUnicode_InternFromString("default_factory");

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
  for (auto &pair : bm_self->instance_data->fields) {
    Py_XDECREF(pair.second);
  }
  delete bm_self->instance_data;
  Py_TYPE(self)->tp_free(self);
}

/**
 * @brief DataModel.__getattro__ implementation.
 *
 * @param self Python object.
 * @param name Attribute name.
 * @return PyObject* Attribute value.
 */
PyObject *DataModel_getattro(PyObject *self, PyObject *name) {
  DataModelObject *bm_self = (DataModelObject *)self;
  InstanceData *data = bm_self->instance_data;

  const char *attr_name = PyUnicode_AsUTF8(name);
  auto it = data->fields.find(attr_name);
  if (it != data->fields.end()) {
    Py_INCREF(it->second);
    return it->second;
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
      auto &fields = data->fields;
      if (fields.find(field_path) != fields.end()) {
        Py_XDECREF(fields[field_path]);
      }
      Py_INCREF(value);
      fields[field_path] = value;
      continue;
    } else {
      Py_DECREF(value);
      value = new_value;
      auto &fields = data->fields;
      if (fields.find(field_path) != fields.end()) {
        Py_XDECREF(fields[field_path]);
      }
      fields[field_path] = value;
    }
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

  // Retrieve the cached schema for the model
  PyObject *capsule = get_schema_cached((PyObject *)Py_TYPE(self));
  if (!capsule) {
    PyErr_SetString(PyExc_TypeError, "Could not retrieve model schema");
    return -1;
  }
  SchemaCache *schema =
      (SchemaCache *)PyCapsule_GetPointer(capsule, "vldt.SchemaCache");
  Py_DECREF(capsule);

  // Use the instance annotations cached in the schema.
  // (Ensure that schema->instance_annotations was set during schema
  // compilation.)
  PyObject *annotations = schema->instance_annotations;
  Py_INCREF(annotations);

  if (annotations && PyDict_Contains(annotations, name)) {
    PyObject *expected_type = PyDict_GetItemWithError(annotations, name);
    if (expected_type) {
      if (is_class_var(expected_type)) {
        PyErr_SetString(PyExc_AttributeError, "Cannot set ClassVar attribute");
        Py_DECREF(annotations);
        return -1;
      } else {
        // Compile a TypeSchema from the expected type
        TypeSchema *ts = compile_type_schema(expected_type);
        if (!ts) {
          Py_DECREF(annotations);
          return -1;
        }
        const char *attr_name = PyUnicode_AsUTF8(name);
        ErrorCollector collector;
        PyObject *converted = validate_and_convert(
            value, ts, &collector, attr_name, schema->deserializers);
        free_type_schema(ts);
        if (!converted) {
          if (collector.has_errors()) {
            std::string err_json = collector.to_json();
            PyErr_SetString(PyExc_TypeError, err_json.c_str());
          } else {
            PyErr_Format(PyExc_TypeError, "Invalid value for attribute %R",
                         name);
          }
          Py_DECREF(annotations);
          return -1;
        }
        // Use the converted value for assignment
        value = converted;
      }
    }
    // Update the instance data with the new value
    const char *attr_name = PyUnicode_AsUTF8(name);
    auto &fields = data->fields;
    if (fields.find(attr_name) != fields.end()) {
      Py_XDECREF(fields[attr_name]);
    }
    fields[attr_name] = value;
    Py_DECREF(annotations);
    return 0;
  } else {
    // If the attribute is not defined in the annotations, assign it directly
    const char *attr_name = PyUnicode_AsUTF8(name);
    auto &fields = data->fields;
    if (fields.find(attr_name) != fields.end()) {
      Py_XDECREF(fields[attr_name]);
    }
    Py_INCREF(value);
    fields[attr_name] = value;
    Py_DECREF(annotations);
    return 0;
  }
}

/**
 * @brief DataModel.__deepcopy__ implementation.
 *
 * @param self Python object.
 * @param args Arguments.
 * @return PyObject* Deep copied object.
 */
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

  for (const auto &pair : src->instance_data->fields) {
    PyObject *copied_field = nullptr;
    PyObject *deepcopy_func =
        PyObject_GetAttrString(pair.second, "__deepcopy__");
    if (deepcopy_func == nullptr) {
      if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
        Py_INCREF(pair.second);
        copied_field = pair.second;
      } else {
        Py_DECREF(new_obj);
        return nullptr;
      }
    } else {
      copied_field = PyObject_CallFunctionObjArgs(deepcopy_func, memo, nullptr);
      Py_DECREF(deepcopy_func);
      if (copied_field == nullptr) {
        Py_DECREF(new_obj);
        return nullptr;
      }
    }
    dst->instance_data->fields[pair.first] = copied_field;
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
