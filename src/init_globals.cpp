#ifdef __cplusplus
extern "C" {
#endif

#include "init_globals.hpp"
#include <Python.h>

PyObject *empty_tuple = nullptr;
PyObject *ClassVarType = nullptr;
PyObject *AnyType = nullptr;
PyObject *UnionType = nullptr;
PyObject *generic_cache = nullptr;
PyObject *TupleType = nullptr;
PyObject *SetType = nullptr;
PyObject *DictType = nullptr;
PyObject *ListType = nullptr;

PyObject *IntType = nullptr;
PyObject *FloatType = nullptr;
PyObject *StrType = nullptr;
PyObject *BoolType = nullptr;
PyObject *NoneType = nullptr;

/**
 * @brief Initialize an empty tuple.
 *
 * @return int 0 on success, -1 on error.
 */
int init_empty_tuple() {
  if (!empty_tuple) {
    empty_tuple = PyTuple_New(0);
    if (!empty_tuple) {
      return -1;
    }
  }
  return 0;
}

/**
 * @brief Initialize ClassVarType from the typing module.
 *
 * @return int 0 on success, -1 on error.
 */
int init_class_var_type() {
  if (!ClassVarType) {
    PyObject *typing_module = PyImport_ImportModule("typing");
    if (!typing_module) {
      return -1;
    }
    ClassVarType = PyObject_GetAttrString(typing_module, "ClassVar");
    Py_DECREF(typing_module);
    if (!ClassVarType) {
      return -1;
    }
  }
  return 0;
}

/**
 * @brief Initialize AnyType from the typing module.
 *
 * @return int 0 on success, -1 on error.
 */
int init_any_type() {
  if (!AnyType) {
    PyObject *typing_module = PyImport_ImportModule("typing");
    if (!typing_module) {
      return -1;
    }
    AnyType = PyObject_GetAttrString(typing_module, "Any");
    Py_DECREF(typing_module);
    if (!AnyType) {
      return -1;
    }
  }
  return 0;
}

/**
 * @brief Initialize container types from the typing module.
 *
 * @return int 0 on success, -1 on error.
 */
int init_container_types() {
  if (!TupleType) {
    PyObject *typing_module = PyImport_ImportModule("typing");
    if (!typing_module) {
      return -1;
    }
    TupleType = PyObject_GetAttrString(typing_module, "Tuple");
    SetType = PyObject_GetAttrString(typing_module, "Set");
    DictType = PyObject_GetAttrString(typing_module, "Dict");
    ListType = PyObject_GetAttrString(typing_module, "List");
    Py_DECREF(typing_module);
    if (!TupleType || !SetType || !DictType || !ListType) {
      return -1;
    }
  }
  return 0;
}

/**
 * @brief Initialize primitive types.
 *
 * @return int 0 on success.
 */
int init_primitive_types() {
  if (!IntType) {
    IntType = (PyObject *)&PyLong_Type;
    FloatType = (PyObject *)&PyFloat_Type;
    StrType = (PyObject *)&PyUnicode_Type;
    BoolType = (PyObject *)&PyBool_Type;
    NoneType = Py_None;
    Py_INCREF(NoneType);
  }
  return 0;
}

/**
 * @brief Ensure UnionType is initialized from the typing module.
 *
 * @return int 0 on success, -1 on error.
 */
int ensure_union_type() {
  if (!UnionType) {
    PyObject *typing_module = PyImport_ImportModule("typing");
    if (!typing_module) {
      return -1;
    }
    UnionType = PyObject_GetAttrString(typing_module, "Union");
    Py_DECREF(typing_module);
    if (!UnionType) {
      return -1;
    }
  }
  return 0;
}

/**
 * @brief Ensure the generic cache is initialized.
 *
 * @return int 0 on success, -1 on error.
 */
int ensure_generic_cache() {
  if (!generic_cache) {
    generic_cache = PyDict_New();
    if (!generic_cache) {
      return -1;
    }
  }
  return 0;
}

PyObject *VLDTUndefined = nullptr;

/**
 * @brief Initialize the VLDTUndefined sentinel.
 *
 * @return int 0 on success, -1 on error.
 */
int init_vldt_undefined() {
  static PyTypeObject VLDTUndefinedType = {
      .ob_base =
          {
              .ob_base =
                  {
                      .ob_refcnt = 1,
                      .ob_type = &PyType_Type,
                  },
              .ob_size = 0,
          },
      .tp_name = "vldt.VLDTUndefined",
      .tp_basicsize = sizeof(PyObject),
      .tp_itemsize = 0,
      .tp_dealloc = nullptr,
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
      .tp_getattro = nullptr,
      .tp_setattro = nullptr,
      .tp_as_buffer = nullptr,
      .tp_flags = Py_TPFLAGS_DEFAULT,
      .tp_doc = "VLDTUndefined sentinel",
      .tp_traverse = nullptr,
      .tp_clear = nullptr,
      .tp_richcompare = nullptr,
      .tp_weaklistoffset = 0,
      .tp_iter = nullptr,
      .tp_iternext = nullptr,
      .tp_methods = nullptr,
      .tp_members = nullptr,
      .tp_getset = nullptr,
      .tp_base = nullptr,
      .tp_dict = nullptr,
      .tp_descr_get = nullptr,
      .tp_descr_set = nullptr,
      .tp_dictoffset = 0,
      .tp_init = nullptr,
      .tp_alloc = nullptr,
      .tp_new = nullptr,
      .tp_free = nullptr,
      .tp_is_gc = nullptr,
      .tp_bases = nullptr,
      .tp_mro = nullptr,
      .tp_cache = nullptr,
      .tp_subclasses = nullptr,
      .tp_weaklist = nullptr,
      .tp_del = nullptr,
      .tp_version_tag = 0,
      .tp_finalize = nullptr,
  };

  if (PyType_Ready(&VLDTUndefinedType) < 0) {
    return -1;
  }

  VLDTUndefined = PyObject_New(PyObject, &VLDTUndefinedType);
  if (!VLDTUndefined) {
    return -1;
  }
  Py_INCREF(VLDTUndefined);
  return 0;
}

/**
 * @brief Combined global initialization for the extension.
 *
 * @return int 0 on success, -1 on error.
 */
int init_extension_globals(void) {
  if (init_empty_tuple() != 0) {
    return -1;
  }
  if (init_class_var_type() != 0) {
    return -1;
  }
  if (init_any_type() != 0) {
    return -1;
  }
  if (ensure_union_type() != 0) {
    return -1;
  }
  if (ensure_generic_cache() != 0) {
    return -1;
  }
  if (init_container_types() != 0) {
    return -1;
  }
  if (init_primitive_types() != 0) {
    return -1;
  }
  if (init_vldt_undefined() != 0) {
    return -1;
  }
  return 0;
}

#ifdef __cplusplus
} // end extern "C"
#endif
