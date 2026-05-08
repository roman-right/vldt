#pragma once

#include <Python.h>

/**
 * @brief Safely retrieves the type name of a PyObject.
 *
 * This function returns the __name__ attribute of a type if available.
 * If the object is not a type or the name cannot be determined, it returns
 * a default "<unknown>" string.
 *
 * @param obj Pointer to a PyObject.
 * @return A const char* representing the type name.
 */
static inline const char *safe_type_name(PyObject *obj) {
  if (!obj) {
    return "<unknown>";
  }
  if (PyType_Check(obj)) {
    PyObject *name_obj = PyObject_GetAttrString(obj, "__name__");
    if (!name_obj) {
      PyErr_Clear();
      name_obj = PyObject_GetAttrString(obj, "__qualname__");
    }
    if (name_obj) {
      const char *name = PyUnicode_AsUTF8(name_obj);
      Py_DECREF(name_obj);
      if (name) {
        return name;
      }
    }
    return ((PyTypeObject *)obj)->tp_name;
  }
  PyTypeObject *type = Py_TYPE(obj);
  if (!type || !type->tp_name) {
    return "<unknown>";
  }
  return type->tp_name;
}
