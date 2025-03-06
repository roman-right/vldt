#include <Python.h>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "data_model.hpp"
#include "init_globals.hpp"
#include "schema/deserializer.hpp"
#include "schema/schema.hpp"

extern PyObject *UnionType;
extern PyObject *ClassVarType;
extern PyObject *TupleType;
extern PyObject *SetType;

namespace {
PyObject *cached_type_schema_key = nullptr;
PyObject *unified_schema_key = nullptr;

/**
 * @brief No-op capsule destructor.
 */
void no_op_capsule_destructor(PyObject *unused) noexcept {}

/**
 * @brief Retrieves the type annotations for a given class.
 * @param cls The class object.
 * @return A new reference to the annotations dictionary.
 *
 * In our updated design we assume that the Python metaclass always sets
 * __vldt_instance_annotations__. If not present or not a dict, an error is raised.
 */
PyObject *get_type_annotations(PyObject *cls) {
  PyObject *inst_annos = PyObject_GetAttrString(cls, "__vldt_instance_annotations__");
  if (!inst_annos || !PyDict_Check(inst_annos)) {
    PyErr_SetString(PyExc_AttributeError,
                    "__vldt_instance_annotations__ is missing or not a dict");
    Py_XDECREF(inst_annos);
    return nullptr;
  }
  return inst_annos;
}

/**
 * @brief Retrieves a cached TypeSchema for the expected type if available.
 * @param expected_type The expected type.
 * @return Pointer to cached TypeSchema or nullptr.
 */
TypeSchema *get_cached_type_schema(PyObject *expected_type) {
  if (PyType_Check(expected_type)) {
    auto type_dict = reinterpret_cast<PyTypeObject *>(expected_type)->tp_dict;
    if (type_dict && PyDict_Check(type_dict)) {
      if (!cached_type_schema_key) {
        cached_type_schema_key =
            PyUnicode_InternFromString("__vldt_type_schema__");
      }
      PyObject *capsule = PyDict_GetItem(type_dict, cached_type_schema_key);
      if (capsule) {
        auto cached_ts = static_cast<TypeSchema *>(
            PyCapsule_GetPointer(capsule, "vldt.TypeSchema"));
        if (cached_ts) {
          return cached_ts;
        }
      }
    }
  }
  return nullptr;
}

/**
 * @brief Caches the TypeSchema for the expected type.
 * @param expected_type The expected type.
 * @param ts The TypeSchema to cache.
 */
void try_cache_type_schema(PyObject *expected_type, TypeSchema *ts) {
  if (PyType_Check(expected_type)) {
    auto type_dict = reinterpret_cast<PyTypeObject *>(expected_type)->tp_dict;
    if (type_dict && PyDict_Check(type_dict)) {
      if (!cached_type_schema_key) {
        cached_type_schema_key =
            PyUnicode_InternFromString("__vldt_type_schema__");
      }
      PyObject *capsule =
          PyCapsule_New(ts, "vldt.TypeSchema", no_op_capsule_destructor);
      if (capsule) {
        PyDict_SetItem(type_dict, cached_type_schema_key, capsule);
        Py_DECREF(capsule);
        ts->cached = 1;
      }
    }
  }
}

/**
 * @brief Handles the case when no origin attribute is available.
 * @param ts The TypeSchema.
 * @param expected_type The expected type.
 * @return The updated TypeSchema.
 */
TypeSchema *handle_no_origin(TypeSchema *ts, PyObject *expected_type) {
  ts->origin = Py_None;
  Py_INCREF(Py_None);
  ts->num_args = 0;
  ts->args = nullptr;
  ts->repr = PyObject_Repr(expected_type);
  if (!ts->repr) {
    ts->repr = Py_None;
    Py_INCREF(Py_None);
  }
  ts->utf8_repr = PyUnicode_AsUTF8(ts->repr);
  try_cache_type_schema(expected_type, ts);
  ts->is_optional = 0;
  return ts;
}

/**
 * @brief Normalizes the origin attribute of the TypeSchema.
 * @param ts The TypeSchema.
 */
void normalize_origin(TypeSchema *ts) {
  if (ts->origin != Py_None) {
    if (PyObject_RichCompareBool(ts->origin, TupleType, Py_EQ) == 1 ||
        PyObject_RichCompareBool(ts->origin, (PyObject *)&PyTuple_Type,
                                 Py_EQ) == 1) {
      Py_DECREF(ts->origin);
      ts->origin = (PyObject *)&PyTuple_Type;
      Py_INCREF(ts->origin);
    } else if (PyObject_RichCompareBool(ts->origin, SetType, Py_EQ) == 1 ||
               PyObject_RichCompareBool(ts->origin, (PyObject *)&PySet_Type,
                                        Py_EQ) == 1) {
      Py_DECREF(ts->origin);
      ts->origin = (PyObject *)&PySet_Type;
      Py_INCREF(ts->origin);
    } else if (PyObject_RichCompareBool(ts->origin, DictType, Py_EQ) == 1) {
      Py_DECREF(ts->origin);
      ts->origin = (PyObject *)&PyDict_Type;
      Py_INCREF(ts->origin);
    }
  }
}

/**
 * @brief Handles the case when there are no arguments.
 * @param ts The TypeSchema.
 * @param expected_type The expected type.
 * @return The updated TypeSchema.
 */
TypeSchema *handle_no_args(TypeSchema *ts, PyObject *expected_type) {
  ts->num_args = 0;
  ts->args = nullptr;
  ts->repr = PyObject_Repr(expected_type);
  if (!ts->repr) {
    ts->repr = Py_None;
    Py_INCREF(Py_None);
  }
  ts->utf8_repr = PyUnicode_AsUTF8(ts->repr);
  try_cache_type_schema(expected_type, ts);
  ts->is_optional = 0;
  return ts;
}

/**
 * @brief Compiles the arguments of a TypeSchema.
 * @param ts The TypeSchema.
 * @param args The arguments tuple.
 * @return 0 on success, -1 on failure.
 */
int compile_args(TypeSchema *ts, PyObject *args) {
  ts->num_args = PyTuple_Size(args);
  if (ts->num_args > 0) {
    std::unique_ptr<TypeSchema *[]> args_array(new (std::nothrow)
                                                   TypeSchema *[ts->num_args]);
    if (!args_array) {
      return -1;
    }
    for (Py_ssize_t i = 0; i < ts->num_args; i++) {
      PyObject *arg_obj = PyTuple_GetItem(args, i);
      args_array[i] = compile_type_schema(arg_obj);
      if (!args_array[i]) {
        for (Py_ssize_t j = 0; j < i; j++) {
          free_type_schema(args_array[j]);
        }
        return -1;
      }
    }
    ts->args = args_array.release();
  } else {
    ts->args = nullptr;
  }
  return 0;
}

/**
 * @brief Determines the container kind for the TypeSchema.
 * @param ts The TypeSchema.
 */
void handle_container_kind(TypeSchema *ts) {
  if (ts->origin && ts->origin != Py_None) {
    if (PyObject_RichCompareBool(ts->origin, UnionType, Py_EQ) == 1) {
      ts->container_kind = CK_UNION;
      ts->is_optional = 0;
      for (Py_ssize_t i = 0; i < ts->num_args; i++) {
        if (ts->args[i]->expected_type == (PyObject *)Py_TYPE(Py_None)) {
          ts->is_optional = 1;
        } else if (PyType_Check(ts->args[i]->expected_type) &&
                   PyObject_IsSubclass(ts->args[i]->expected_type,
                                       (PyObject *)&DataModelType) == 1) {
          ts->inner_model_type = ts->args[i]->expected_type;
          Py_INCREF(ts->inner_model_type);
        }
      }
    } else if (PyObject_RichCompareBool(ts->origin, (PyObject *)&PyDict_Type,
                                        Py_EQ) == 1 &&
               ts->num_args == 2) {
      ts->container_kind = CK_DICT;
      if (PyType_Check(ts->args[1]->expected_type) &&
          PyObject_IsSubclass(ts->args[1]->expected_type,
                              (PyObject *)&DataModelType) == 1) {
        ts->inner_model_type = ts->args[1]->expected_type;
        Py_INCREF(ts->inner_model_type);
      }
    } else if (PyObject_RichCompareBool(ts->origin, (PyObject *)&PyList_Type,
                                        Py_EQ) == 1 &&
               ts->num_args == 1) {
      ts->container_kind = CK_LIST;
      if (PyType_Check(ts->args[0]->expected_type) &&
          PyObject_IsSubclass(ts->args[0]->expected_type,
                              (PyObject *)&DataModelType) == 1) {
        ts->inner_model_type = ts->args[0]->expected_type;
        Py_INCREF(ts->inner_model_type);
      }
    } else if (PyObject_RichCompareBool(ts->origin, (PyObject *)&PyTuple_Type,
                                        Py_EQ) == 1) {
      ts->container_kind = CK_TUPLE;
      if (ts->num_args == 1) {
        if (PyType_Check(ts->args[0]->expected_type) &&
            PyObject_IsSubclass(ts->args[0]->expected_type,
                                (PyObject *)&DataModelType) == 1) {
          ts->inner_model_type = ts->args[0]->expected_type;
          Py_INCREF(ts->inner_model_type);
        }
      }
    } else if (PyObject_RichCompareBool(ts->origin, (PyObject *)&PySet_Type,
                                        Py_EQ) == 1) {
      ts->container_kind = CK_SET;
      if (ts->num_args == 1) {
        if (PyType_Check(ts->args[0]->expected_type) &&
            PyObject_IsSubclass(ts->args[0]->expected_type,
                                (PyObject *)&DataModelType) == 1) {
          ts->inner_model_type = ts->args[0]->expected_type;
          Py_INCREF(ts->inner_model_type);
        }
      }
    } else {
      ts->container_kind = CK_NONE;
      ts->inner_model_type = nullptr;
    }
  } else {
    ts->container_kind = CK_NONE;
    ts->inner_model_type = nullptr;
  }
}
} // anonymous namespace

/**
 * @brief Compiles the type schema for the expected type.
 * @param expected_type The expected type.
 * @return Pointer to the compiled TypeSchema or nullptr on error.
 */
TypeSchema *compile_type_schema(PyObject *expected_type) {
  if (!expected_type) {
    return nullptr;
  }
  if (auto cached = get_cached_type_schema(expected_type)) {
    return cached;
  }
  auto ts = new (std::nothrow) TypeSchema{};
  if (!ts) {
    PyErr_NoMemory();
    return nullptr;
  }
  ts->expected_type = expected_type;
  Py_INCREF(expected_type);
  ts->cached = 0;
  ts->is_data_model = 0;
  ts->container_kind = CK_NONE;
  ts->inner_model_type = nullptr;
  if (PyType_Check(expected_type)) {
    int is_sub = PyObject_IsSubclass(expected_type, (PyObject *)&DataModelType);
    if (is_sub < 0) {
      PyErr_Clear();
    } else if (is_sub) {
      ts->is_data_model = 1;
    }
  }
  PyObject *origin = PyObject_GetAttrString(expected_type, "__origin__");
  if (!origin) {
    PyErr_Clear();
    return handle_no_origin(ts, expected_type);
  }
  ts->origin = origin;
  normalize_origin(ts);
  PyObject *args = PyObject_GetAttrString(expected_type, "__args__");
  if (!args || !PyTuple_Check(args)) {
    if (args) {
      Py_DECREF(args);
    }
    return handle_no_args(ts, expected_type);
  }
  if (compile_args(ts, args) != 0) {
    Py_DECREF(args);
    free_type_schema(ts);
    return nullptr;
  }
  Py_DECREF(args);
  ts->repr = PyObject_Repr(expected_type);
  if (!ts->repr) {
    ts->repr = Py_None;
    Py_INCREF(Py_None);
  }
  ts->utf8_repr = PyUnicode_AsUTF8(ts->repr);
  handle_container_kind(ts);
  try_cache_type_schema(expected_type, ts);
  return ts;
}

/**
 * @brief Frees the allocated TypeSchema.
 * @param ts Pointer to the TypeSchema.
 */
void free_type_schema(TypeSchema *ts) {
  if (!ts) {
    return;
  }
  if (ts->cached) {
    return;
  }
  Py_DECREF(ts->expected_type);
  Py_DECREF(ts->origin);
  Py_DECREF(ts->repr);
  if (ts->inner_model_type) {
    Py_DECREF(ts->inner_model_type);
  }
  if (ts->args) {
    for (Py_ssize_t i = 0; i < ts->num_args; i++) {
      free_type_schema(ts->args[i]);
    }
    delete[] ts->args;
  }
  delete ts;
}

namespace {
/**
 * @brief Counts the number of non-class variable annotations.
 * @param annotations The annotations dictionary.
 * @return The count of non-class variables.
 */
Py_ssize_t count_non_class_vars(PyObject *annotations) {
  Py_ssize_t count = 0;
  Py_ssize_t pos = 0;
  PyObject *key, *expected_type;
  while (PyDict_Next(annotations, &pos, &key, &expected_type)) {
    int is_class_var = 0;
    PyObject *origin = PyObject_GetAttrString(expected_type, "__origin__");
    if (origin) {
      if (origin == ClassVarType) {
        is_class_var = 1;
      }
      Py_DECREF(origin);
    } else {
      PyErr_Clear();
    }
    if (!is_class_var) {
      count++;
    }
  }
  return count;
}

/**
 * @brief Compiles the field schema for a given field.
 * @param cls The class object.
 * @param key The field name.
 * @param expected_type The expected type.
 * @param fs Pointer to the FieldSchema.
 * @return 0 on success.
 */
int compile_field_schema(PyObject *cls, PyObject *key, PyObject *expected_type,
                         FieldSchema *fs) {
  fs->field_name = key;
  Py_INCREF(key);
  fs->field_name_c = PyUnicode_AsUTF8(key);
  fs->alias = nullptr;
  fs->default_value = VLDTUndefined;
  Py_INCREF(VLDTUndefined);
  fs->default_factory = Py_None;
  Py_INCREF(Py_None);
  const char *key_str = PyUnicode_AsUTF8(key);
  PyObject *field_obj = nullptr;
  if (PyObject_HasAttrString(cls, key_str)) {
    field_obj = PyObject_GetAttrString(cls, key_str);
  }
  if (field_obj) {
    int has_descriptor_attrs = 0;
    if (PyObject_HasAttrString(field_obj, "default") ||
        PyObject_HasAttrString(field_obj, "default_factory")) {
      has_descriptor_attrs = 1;
    }
    if (has_descriptor_attrs) {
      PyObject *alias_obj = PyObject_GetAttrString(field_obj, "alias");
      if (alias_obj) {
        if (!PyList_Check(alias_obj)) {
          if (PyUnicode_Check(alias_obj)) {
            PyObject *tmp = PyList_New(1);
            if (!tmp) {
              Py_DECREF(alias_obj);
              alias_obj = nullptr;
            } else {
              PyList_SET_ITEM(tmp, 0, alias_obj);
              alias_obj = tmp;
            }
          }
        }
        fs->alias = alias_obj;
      } else {
        fs->alias = nullptr;
      }
      PyObject *factory = PyObject_GetAttrString(field_obj, "default_factory");
      if (factory && factory != Py_None && PyCallable_Check(factory)) {
        fs->default_factory = factory;
      } else {
        Py_XDECREF(factory);
        PyObject *def_val = PyObject_GetAttrString(field_obj, "default");
        if (def_val) {
          Py_DECREF(fs->default_value);
          fs->default_value = def_val;
        }
      }
    } else {
      Py_DECREF(fs->default_value);
      fs->default_value = field_obj;
      Py_INCREF(fs->default_value);
    }
    Py_DECREF(field_obj);
  }
  fs->type_schema = compile_type_schema(expected_type);
  return 0;
}

/**
 * @brief Compiles the configuration for the schema.
 * @param cls The class object.
 * @param schema Pointer to the SchemaCache.
 */
void compile_config(PyObject *cls, SchemaCache *schema) {
  PyObject *config = PyObject_GetAttrString(cls, "__vldt_config__");
  if (config) {
    PyObject *dict_enc = nullptr;
    if (PyDict_Check(config)) {
      dict_enc = PyDict_GetItemString(config, "dict_serializer");
      if (dict_enc) {
        Py_INCREF(dict_enc);
      }
    } else {
      dict_enc = PyObject_GetAttrString(config, "dict_serializer");
    }
    if (dict_enc) {
      schema->dict_serializer = dict_enc;
    } else {
      schema->dict_serializer = Py_None;
      Py_INCREF(Py_None);
    }
    PyObject *json_enc = nullptr;
    if (PyDict_Check(config)) {
      json_enc = PyDict_GetItemString(config, "json_serializer");
      if (json_enc) {
        Py_INCREF(json_enc);
      }
    } else {
      json_enc = PyObject_GetAttrString(config, "json_serializer");
    }
    if (json_enc) {
      schema->json_serializer = json_enc;
    } else {
      schema->json_serializer = Py_None;
      Py_INCREF(Py_None);
    }
    PyObject *deserializer_obj = nullptr;
    if (PyDict_Check(config)) {
      deserializer_obj = PyDict_GetItemString(config, "deserializer");
      if (deserializer_obj) {
        Py_INCREF(deserializer_obj);
      }
    } else {
      deserializer_obj = PyObject_GetAttrString(config, "deserializer");
    }
    if (deserializer_obj && PyDict_Check(deserializer_obj)) {
      schema->deserializers = create_deserializers(deserializer_obj);
      if (!schema->deserializers) {
        Py_DECREF(deserializer_obj);
        schema->deserializers = nullptr;
      }
    } else {
      schema->deserializers = nullptr;
    }
    Py_XDECREF(deserializer_obj);
    schema->config = config;
  } else {
    schema->config = Py_None;
    Py_INCREF(Py_None);
    schema->dict_serializer = Py_None;
    Py_INCREF(Py_None);
    schema->json_serializer = Py_None;
    Py_INCREF(Py_None);
    schema->deserializers = nullptr;
  }
}

/**
 * @brief Compiles validators for the schema.
 * @param cls The class object.
 * @param schema Pointer to the SchemaCache.
 */
void compile_validators(PyObject *cls, SchemaCache *schema) {
  PyObject *validators = PyObject_GetAttrString(cls, "__vldt_validators__");
  if (validators && PyDict_Check(validators)) {
    schema->validators = validators;
    PyObject *tmp =
        PyObject_GetAttrString(cls, "__vldt_has_field_before_validators__");
    schema->has_field_before = tmp ? PyObject_IsTrue(tmp) : 0;
    Py_XDECREF(tmp);
    tmp = PyObject_GetAttrString(cls, "__vldt_has_field_after_validators__");
    schema->has_field_after = tmp ? PyObject_IsTrue(tmp) : 0;
    Py_XDECREF(tmp);
    tmp = PyObject_GetAttrString(cls, "__vldt_has_model_before_validators__");
    schema->has_model_before = tmp ? PyObject_IsTrue(tmp) : 0;
    Py_XDECREF(tmp);
    tmp = PyObject_GetAttrString(cls, "__vldt_has_model_after_validators__");
    schema->has_model_after = tmp ? PyObject_IsTrue(tmp) : 0;
    Py_XDECREF(tmp);
  } else {
    schema->validators = Py_None;
    Py_INCREF(Py_None);
    schema->has_field_before = 0;
    schema->has_field_after = 0;
    schema->has_model_before = 0;
    schema->has_model_after = 0;
  }
}
} // anonymous namespace

/**
 * @brief Compiles the schema for the class.
 * @param cls The class object.
 * @return A new capsule containing the SchemaCache.
 */
PyObject *compile_schema(PyObject *cls) {
  PyObject *annotations = get_type_annotations(cls);
  if (!annotations || !PyDict_Check(annotations)) {
    Py_XDECREF(annotations);
    return nullptr;
  }
  Py_ssize_t count = count_non_class_vars(annotations);
  auto schema = new (std::nothrow) SchemaCache{};
  if (!schema) {
    Py_DECREF(annotations);
    PyErr_NoMemory();
    return nullptr;
  }
  schema->num_fields = count;
  schema->fields = new (std::nothrow) FieldSchema[count];
  if (!schema->fields) {
    delete schema;
    Py_DECREF(annotations);
    PyErr_NoMemory();
    return nullptr;
  }
  Py_ssize_t pos = 0, idx = 0;
  PyObject *key, *expected_type;
  while (PyDict_Next(annotations, &pos, &key, &expected_type)) {
    int is_class_var = 0;
    PyObject *origin = PyObject_GetAttrString(expected_type, "__origin__");
    if (origin) {
      if (origin == ClassVarType) {
        is_class_var = 1;
      }
      Py_DECREF(origin);
    } else {
      PyErr_Clear();
    }
    if (is_class_var) {
      continue;
    }
    FieldSchema *fs = &schema->fields[idx];
    compile_field_schema(cls, key, expected_type, fs);
    idx++;
  }
  Py_DECREF(annotations);
  compile_config(cls, schema);

  // Directly retrieve __vldt_instance_annotations__; the Python metaclass is expected
  // to have set this correctly.
  schema->instance_annotations = PyObject_GetAttrString(cls, "__vldt_instance_annotations__");
  if (!schema->instance_annotations || !PyDict_Check(schema->instance_annotations)) {
    PyErr_SetString(PyExc_AttributeError,
                    "__vldt_instance_annotations__ must be set and be a dict");
    Py_XDECREF(schema->instance_annotations);
    schema->instance_annotations = Py_None;
    Py_INCREF(Py_None);
  }

  compile_validators(cls, schema);
  schema->cached_to_dict = PyObject_GetAttrString(cls, "to_dict");
  PyObject *capsule = PyCapsule_New(
      static_cast<void *>(schema), "vldt.SchemaCache", [](PyObject *capsule) {
        auto schema = static_cast<SchemaCache *>(
            PyCapsule_GetPointer(capsule, "vldt.SchemaCache"));
        if (schema) {
          for (Py_ssize_t i = 0; i < schema->num_fields; i++) {
            FieldSchema *fs = &schema->fields[i];
            Py_DECREF(fs->field_name);
            if (fs->alias) {
              Py_DECREF(fs->alias);
            }
            Py_DECREF(fs->default_value);
            Py_DECREF(fs->default_factory);
            if (fs->type_schema) {
              free_type_schema(fs->type_schema);
            }
          }
          delete[] schema->fields;
          Py_DECREF(schema->config);
          Py_DECREF(schema->dict_serializer);
          Py_DECREF(schema->json_serializer);
          Py_DECREF(schema->instance_annotations);
          Py_DECREF(schema->validators);
          Py_DECREF(schema->cached_to_dict);
          if (schema->deserializers) {
            free_deserializers(schema->deserializers);
          }
          delete schema;
        }
      });
  return capsule;
}

/**
 * @brief Retrieves a cached schema for the class if available.
 * @param cls The class object.
 * @return A new reference to the schema capsule.
 */
PyObject *get_schema_cached(PyObject *cls) {
  auto type_dict = reinterpret_cast<PyTypeObject *>(cls)->tp_dict;
  if (type_dict && PyDict_Check(type_dict)) {
    if (!unified_schema_key) {
      unified_schema_key = PyUnicode_InternFromString("__vldt_schema__");
    }
    PyObject *capsule = PyDict_GetItem(type_dict, unified_schema_key);
    if (capsule) {
      Py_INCREF(capsule);
      return capsule;
    }
  }
  PyObject *capsule = compile_schema(cls);
  if (!capsule) {
    return nullptr;
  }
  if (type_dict && PyDict_Check(type_dict)) {
    if (!unified_schema_key) {
      unified_schema_key = PyUnicode_InternFromString("__vldt_schema__");
    }
    PyDict_SetItem(type_dict, unified_schema_key, capsule);
  }
  return capsule;
}
