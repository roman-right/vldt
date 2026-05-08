#include <Python.h>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "data_model.hpp"
#include "init_globals.hpp"
#include "schema/deserializer.hpp"
#include "schema/schema.hpp"

extern PyObject *UnionType;
extern PyObject *LiteralType;
extern PyObject *ClassVarType;
extern PyObject *TupleType;
extern PyObject *SetType;
extern PyObject *FieldType;
extern PyObject *FieldUndefined;

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
 * __vldt_instance_annotations__. If not present or not a dict, an error is
 * raised.
 */
PyObject *get_type_annotations(PyObject *cls) {
  PyObject *inst_annos =
      PyObject_GetAttrString(cls, "__vldt_instance_annotations__");
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
    if (ts->origin == TupleType ||
        ts->origin == (PyObject *)&PyTuple_Type) {
      Py_DECREF(ts->origin);
      ts->origin = (PyObject *)&PyTuple_Type;
      Py_INCREF(ts->origin);
    } else if (ts->origin == SetType ||
               ts->origin == (PyObject *)&PySet_Type) {
      Py_DECREF(ts->origin);
      ts->origin = (PyObject *)&PySet_Type;
      Py_INCREF(ts->origin);
    } else if (ts->origin == DictType ||
               ts->origin == (PyObject *)&PyDict_Type) {
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
    if (ts->origin == UnionType) {
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
    } else if (ts->origin == (PyObject *)&PyDict_Type && ts->num_args == 2) {
      ts->container_kind = CK_DICT;
      if (PyType_Check(ts->args[1]->expected_type) &&
          PyObject_IsSubclass(ts->args[1]->expected_type,
                              (PyObject *)&DataModelType) == 1) {
        ts->inner_model_type = ts->args[1]->expected_type;
        Py_INCREF(ts->inner_model_type);
      }
    } else if (ts->origin == (PyObject *)&PyList_Type && ts->num_args == 1) {
      ts->container_kind = CK_LIST;
      if (PyType_Check(ts->args[0]->expected_type) &&
          PyObject_IsSubclass(ts->args[0]->expected_type,
                              (PyObject *)&DataModelType) == 1) {
        ts->inner_model_type = ts->args[0]->expected_type;
        Py_INCREF(ts->inner_model_type);
      }
    } else if (ts->origin == (PyObject *)&PyTuple_Type) {
      ts->container_kind = CK_TUPLE;
      // Detect Tuple[T, ...] (variadic). The Python typing module compiles
      // this as Tuple[T, Ellipsis], so the second arg's expected_type is
      // the Ellipsis singleton. Collapse it to a single-arg variadic schema.
      if (ts->num_args == 2 && ts->args[1] != nullptr &&
          ts->args[1]->expected_type == Py_Ellipsis) {
        ts->is_variadic_tuple = 1;
        free_type_schema(ts->args[1]);
        ts->args[1] = nullptr;
        ts->num_args = 1;
      }
      if (ts->num_args == 1 && ts->args[0] != nullptr) {
        if (PyType_Check(ts->args[0]->expected_type) &&
            PyObject_IsSubclass(ts->args[0]->expected_type,
                                (PyObject *)&DataModelType) == 1) {
          ts->inner_model_type = ts->args[0]->expected_type;
          Py_INCREF(ts->inner_model_type);
        }
      }
    } else if (ts->origin == (PyObject *)&PySet_Type) {
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
  ts->primitive_kind = PK_NONE;
  ts->is_variadic_tuple = 0;
  ts->is_literal = 0;
  ts->literal_values = nullptr;
  ts->inner_model_type = nullptr;
  if (PyType_Check(expected_type)) {
    int is_sub = PyObject_IsSubclass(expected_type, (PyObject *)&DataModelType);
    if (is_sub < 0) {
      PyErr_Clear();
    } else if (is_sub) {
      ts->is_data_model = 1;
    }
  }
  // Cache primitive kind for fast dispatch in hot loops. Bool first because
  // bool is also a subclass of int. Any is matched against AnyType.
  if (expected_type == BoolType) {
    ts->primitive_kind = PK_BOOL;
  } else if (expected_type == IntType) {
    ts->primitive_kind = PK_INT;
  } else if (expected_type == FloatType) {
    ts->primitive_kind = PK_FLOAT;
  } else if (expected_type == StrType) {
    ts->primitive_kind = PK_STR;
  } else if (expected_type == AnyType) {
    ts->primitive_kind = PK_ANY;
  }
  PyObject *origin = PyObject_GetAttrString(expected_type, "__origin__");
  if (!origin) {
    PyErr_Clear();
    return handle_no_origin(ts, expected_type);
  }
  ts->origin = origin;
  // typing.Literal[...] needs special handling: __args__ holds VALUES,
  // not types, so we must not recursively compile_args them. Store the
  // tuple as-is for membership comparison at validation time and short
  // circuit the rest of the generic-type compilation.
  if (LiteralType && origin == LiteralType) {
    PyObject *lit_args = PyObject_GetAttrString(expected_type, "__args__");
    if (!lit_args || !PyTuple_Check(lit_args)) {
      Py_XDECREF(lit_args);
      return handle_no_args(ts, expected_type);
    }
    ts->is_literal = 1;
    ts->literal_values = lit_args; // strong ref, freed in free_type_schema
    ts->num_args = 0;
    ts->args = nullptr;
    ts->repr = PyObject_Repr(expected_type);
    if (!ts->repr) {
      ts->repr = Py_None;
      Py_INCREF(Py_None);
    }
    ts->utf8_repr = PyUnicode_AsUTF8(ts->repr);
    try_cache_type_schema(expected_type, ts);
    return ts;
  }
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
 *
 * Each field is null-guarded so that partially-constructed TypeSchemas
 * (resulting from a mid-compilation failure such as OOM) can be safely
 * cleaned up without dereferencing nullptr.
 */
void free_type_schema(TypeSchema *ts) {
  if (!ts) {
    return;
  }
  if (ts->cached) {
    return;
  }
  Py_XDECREF(ts->expected_type);
  Py_XDECREF(ts->origin);
  Py_XDECREF(ts->repr);
  Py_XDECREF(ts->inner_model_type);
  Py_XDECREF(ts->literal_values);
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
 *
 * Snapshots the items of `annotations` into a tuple before iterating. The
 * loop body calls PyObject_GetAttrString on the annotation type, which can
 * dispatch to user-defined __getattribute__ code; if that code mutated the
 * source dict, a concurrent PyDict_Next would corrupt iteration. Iterating
 * a stable tuple sidesteps the issue.
 *
 * @param annotations The annotations dictionary.
 * @return The count of non-class variables, or -1 on error.
 */
Py_ssize_t count_non_class_vars(PyObject *annotations) {
  PyObject *items = PyDict_Items(annotations);
  if (!items) {
    return -1;
  }
  Py_ssize_t count = 0;
  Py_ssize_t n = PyList_GET_SIZE(items);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *pair = PyList_GET_ITEM(items, i);
    PyObject *expected_type = PyTuple_GET_ITEM(pair, 1);
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
  Py_DECREF(items);
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
  Py_ssize_t fname_len = 0;
  fs->field_name_c = PyUnicode_AsUTF8AndSize(key, &fname_len);
  fs->field_name_len = fname_len;
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
    // Use a proper isinstance check against the canonical Field type so that
    // user objects which happen to expose 'default' or 'default_factory' are
    // not misinterpreted as Field descriptors.
    int is_field = (FieldType != nullptr) &&
                   PyObject_IsInstance(field_obj, FieldType) == 1;
    if (is_field) {
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
          // Treat the Python UNDEFINED sentinel from vldt.fields as
          // "no default provided" so a bare Field() leaves default_value
          // as VLDTUndefined.
          if (def_val == FieldUndefined) {
            Py_DECREF(def_val);
          } else {
            Py_DECREF(fs->default_value);
            fs->default_value = def_val;
          }
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
    // Read the `extra` policy. Default to ignore if absent or unparsable
    // so existing models keep their pre-policy behaviour.
    schema->extra_policy = EXTRA_IGNORE;
    PyObject *extra_obj = nullptr;
    if (PyDict_Check(config)) {
      extra_obj = PyDict_GetItemString(config, "extra");
      Py_XINCREF(extra_obj);
    } else {
      extra_obj = PyObject_GetAttrString(config, "extra");
      if (!extra_obj) {
        PyErr_Clear();
      }
    }
    if (extra_obj && PyUnicode_Check(extra_obj)) {
      if (PyUnicode_CompareWithASCIIString(extra_obj, "forbid") == 0) {
        schema->extra_policy = EXTRA_FORBID;
      } else if (PyUnicode_CompareWithASCIIString(extra_obj, "allow") == 0) {
        schema->extra_policy = EXTRA_ALLOW;
      }
    }
    Py_XDECREF(extra_obj);
    schema->config = config;
  } else {
    schema->config = Py_None;
    Py_INCREF(Py_None);
    schema->dict_serializer = Py_None;
    Py_INCREF(Py_None);
    schema->json_serializer = Py_None;
    Py_INCREF(Py_None);
    schema->deserializers = nullptr;
    schema->extra_policy = EXTRA_IGNORE;
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
  if (count < 0) {
    Py_DECREF(annotations);
    return nullptr;
  }
  auto schema = new (std::nothrow) SchemaCache{};
  if (!schema) {
    Py_DECREF(annotations);
    PyErr_NoMemory();
    return nullptr;
  }
  schema->num_fields = count;
  schema->fields = (count > 0) ? new (std::nothrow) FieldSchema[count] : nullptr;
  if (count > 0 && !schema->fields) {
    delete schema;
    Py_DECREF(annotations);
    PyErr_NoMemory();
    return nullptr;
  }
  // Snapshot the annotations into a stable tuple list before iterating.
  // The body calls PyObject_GetAttrString on each annotation type, which
  // can run user-defined __getattribute__ code; iterating the snapshot
  // makes that callback safe even if the source dict were to be mutated
  // (defensive correctness for issue #13).
  PyObject *items = PyDict_Items(annotations);
  Py_DECREF(annotations);
  if (!items) {
    delete[] schema->fields;
    delete schema;
    return nullptr;
  }
  Py_ssize_t n_items = PyList_GET_SIZE(items);
  Py_ssize_t idx = 0;
  for (Py_ssize_t i = 0; i < n_items; i++) {
    PyObject *pair = PyList_GET_ITEM(items, i);
    PyObject *key = PyTuple_GET_ITEM(pair, 0);
    PyObject *expected_type = PyTuple_GET_ITEM(pair, 1);
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
  Py_DECREF(items);
  compile_config(cls, schema);

  // Directly retrieve __vldt_instance_annotations__; the Python metaclass is
  // expected to have set this correctly.
  schema->instance_annotations =
      PyObject_GetAttrString(cls, "__vldt_instance_annotations__");
  if (!schema->instance_annotations ||
      !PyDict_Check(schema->instance_annotations)) {
    PyErr_SetString(PyExc_AttributeError,
                    "__vldt_instance_annotations__ must be set and be a dict");
    Py_XDECREF(schema->instance_annotations);
    schema->instance_annotations = Py_None;
    Py_INCREF(Py_None);
  }

  compile_validators(cls, schema);
  schema->cached_to_dict = PyObject_GetAttrString(cls, "to_dict");

  // Populate the name -> field-index map (canonical names + any aliases).
  // First-writer wins, matching the order: aliases listed earlier on the
  // same field take precedence over the canonical name only because the
  // walk path checks aliases first; here we simply ensure every name
  // resolves to its owning field.
  for (Py_ssize_t i = 0; i < schema->num_fields; i++) {
    FieldSchema *fs = &schema->fields[i];
    if (fs->field_name_c) {
      schema->name_index.emplace(
          std::string(fs->field_name_c,
                      static_cast<size_t>(fs->field_name_len)),
          i);
    }
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
        schema->name_index.emplace(
            std::string(alias_c, static_cast<size_t>(alias_len)), i);
      }
    }
  }
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
