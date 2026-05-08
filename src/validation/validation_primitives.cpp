#include "validation_primitives.hpp"
#include "error_handling.hpp"
#include "init_globals.hpp"
#include "safe_type_name.hpp"

#include <Python.h>
#include <array>
#include <stdio.h>
#include <string>

// extern PyObject *IntType;
// extern PyObject *StrType;
// extern PyObject *FloatType;
// extern PyObject *BoolType;

/**
 * @brief Validates and converts a Python object to an integer.
 *
 * If the object is already an integer, it returns the object with an
 * incremented reference. Otherwise, it attempts to convert the object to an
 * integer using IntType. On failure, it adds an error to the collector.
 *
 * @param value The Python object to validate.
 * @param collector The error collector.
 * @param error_path The error path.
 * @return A new reference to the integer, or nullptr on error.
 */
PyObject *validate_int(PyObject *value, ErrorCollector *collector,
                       const char *error_path) {
  if (PyLong_Check(value)) {
    Py_INCREF(value);
    return value;
  } else {
    PyObject *conv = PyObject_CallFunctionObjArgs(IntType, value, nullptr);
    if (conv && PyLong_Check(conv)) {
      return conv;
    }
    Py_XDECREF(conv);
    PyErr_Clear();
    if (collector) {
      collector->add_error(error_path, std::string("Expected type int, got ") +
                                           safe_type_name(value));
    }
    return nullptr;
  }
}

/**
 * @brief Validates and converts a Python object to a string.
 *
 * If the object is already a Unicode string, it returns the object with an
 * incremented reference. Otherwise, it attempts to convert the object to a
 * string using StrType. On failure, it adds an error to the collector.
 *
 * @param value The Python object to validate.
 * @param collector The error collector.
 * @param error_path The error path.
 * @return A new reference to the string, or nullptr on error.
 */
PyObject *validate_str(PyObject *value, ErrorCollector *collector,
                       const char *error_path) {
  if (PyUnicode_Check(value)) {
    Py_INCREF(value);
    return value;
  } else {
    PyObject *conv = PyObject_CallFunctionObjArgs(StrType, value, nullptr);
    if (conv && PyUnicode_Check(conv)) {
      return conv;
    }
    Py_XDECREF(conv);
    PyErr_Clear();
    if (collector) {
      collector->add_error(error_path, std::string("Expected type str, got ") +
                                           safe_type_name(value));
    }
    return nullptr;
  }
}

/**
 * @brief Validates and converts a Python object to a float.
 *
 * If the object is already a float, it returns the object with an incremented
 * reference. Otherwise, it attempts to convert the object to a float using
 * FloatType. On failure, it adds an error to the collector.
 *
 * @param value The Python object to validate.
 * @param collector The error collector.
 * @param error_path The error path.
 * @return A new reference to the float, or nullptr on error.
 */
PyObject *validate_float(PyObject *value, ErrorCollector *collector,
                         const char *error_path) {
  if (PyFloat_Check(value)) {
    Py_INCREF(value);
    return value;
  } else {
    PyObject *conv = PyObject_CallFunctionObjArgs(FloatType, value, nullptr);
    if (conv && PyFloat_Check(conv)) {
      return conv;
    }
    Py_XDECREF(conv);
    PyErr_Clear();
    if (collector) {
      collector->add_error(error_path,
                           std::string("Expected type float, got ") +
                               safe_type_name(value));
    }
    return nullptr;
  }
}

/**
 * @brief Validates and converts a Python object to a boolean.
 *
 * If the object is already a boolean, it returns the object with an incremented
 * reference. Otherwise, it attempts to convert the object to a boolean using
 * BoolType. On failure, it adds an error to the collector.
 *
 * @param value The Python object to validate.
 * @param collector The error collector.
 * @param error_path The error path.
 * @return A new reference to the boolean, or nullptr on error.
 */
PyObject *validate_bool(PyObject *value, ErrorCollector *collector,
                        const char *error_path) {
  if (PyBool_Check(value)) {
    Py_INCREF(value);
    return value;
  } else {
    PyObject *conv = PyObject_CallFunctionObjArgs(BoolType, value, nullptr);
    if (conv && PyBool_Check(conv)) {
      return conv;
    }
    Py_XDECREF(conv);
    PyErr_Clear();
    if (collector) {
      collector->add_error(error_path, std::string("Expected type bool, got ") +
                                           safe_type_name(value));
    }
    return nullptr;
  }
}
