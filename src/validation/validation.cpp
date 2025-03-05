#include <Python.h>
#include <array>
#include <stdio.h>
#include <string>

#include "error_handling.hpp"
#include "init_globals.hpp"
#include "schema/deserializer.hpp"
#include "schema/schema.hpp"
#include "validation.hpp"
#include "validation_containers.hpp"
#include "validation_primitives.hpp"

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
static const char *safe_type_name(PyObject *obj) {
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

/**
 * @brief Validates and converts a DataModel from a Python dictionary.
 *
 * This function attempts to construct a new object of the expected type by
 * calling its constructor with the provided value. If an exception occurs,
 * the error is captured and reported via the ErrorCollector.
 *
 * @param value The Python object to validate.
 * @param ts Pointer to the TypeSchema describing the expected type.
 * @param collector Pointer to an ErrorCollector for reporting errors.
 * @param error_path A string representing the path for error messages.
 * @param deserializers Pointer to registered deserializers.
 * @return A new PyObject on success, or nullptr on error.
 */
static PyObject *validate_data_model(PyObject *value, TypeSchema *ts,
                                     ErrorCollector *collector,
                                     const char *error_path,
                                     Deserializers *deserializers) {
  PyObject *converted = PyObject_Call(ts->expected_type, empty_tuple, value);
  if (converted) {
    return converted;
  } else {
    PyObject *exc_type = nullptr, *exc_value = nullptr, *exc_tb = nullptr;
    PyErr_Fetch(&exc_type, &exc_value, &exc_tb);
    PyObject *exc_str = PyObject_Str(exc_value);
    const char *nested_json =
        exc_str ? PyUnicode_AsUTF8(exc_str) : "Unknown error";
    if (collector) {
      collector->add_suberror(error_path, nested_json);
    }
    Py_XDECREF(exc_str);
    PyErr_Clear();
    return nullptr;
  }
}

/**
 * @brief Validates and converts a plain Python object to the expected type.
 *
 * If the value is already an instance of the expected type, it is returned.
 * Otherwise, a registered deserializer is attempted, and if that fails,
 * the function attempts specific validations for basic types.
 *
 * @param value The Python object to validate.
 * @param ts Pointer to the TypeSchema describing the expected type.
 * @param collector Pointer to an ErrorCollector for reporting errors.
 * @param error_path A string representing the path for error messages.
 * @param deserializers Pointer to registered deserializers.
 * @return A new PyObject on success, or nullptr on error.
 */
static PyObject *validate_plain(PyObject *value, TypeSchema *ts,
                                ErrorCollector *collector,
                                const char *error_path,
                                Deserializers *deserializers) {
  if (PyObject_IsInstance(value, ts->expected_type)) {
    Py_INCREF(value);
    return value;
  } else {
    PyObject *deserializer_func = nullptr;
    if (deserializers) {
      deserializer_func = get_deserializer(deserializers, ts->expected_type,
                                           (PyObject *)Py_TYPE(value));
    }
    if (deserializer_func && deserializer_func != Py_None) {
      PyObject *deserialized =
          PyObject_CallFunctionObjArgs(deserializer_func, value, nullptr);
      if (deserialized &&
          PyObject_IsInstance(deserialized, ts->expected_type)) {
        return deserialized;
      }
      Py_XDECREF(deserialized);
      PyErr_Clear();
    }

    if (ts->expected_type == IntType) {
      return validate_int(value, collector, error_path);
    }
    if (ts->expected_type == StrType) {
      return validate_str(value, collector, error_path);
    }
    if (ts->expected_type == FloatType) {
      return validate_float(value, collector, error_path);
    }
    if (ts->expected_type == BoolType) {
      return validate_bool(value, collector, error_path);
    }

    PyObject *conv =
        PyObject_CallFunctionObjArgs(ts->expected_type, value, nullptr);
    if (conv && PyObject_IsInstance(conv, ts->expected_type)) {
      return conv;
    }
    Py_XDECREF(conv);
    PyErr_Clear();
    if (collector) {
      collector->add_error(error_path, std::string("Expected type ") +
                                           safe_type_name(ts->expected_type) +
                                           ", got " + safe_type_name(value));
    }
    return nullptr;
  }
}

/**
 * @brief Converts a Python object using its expected type constructor.
 *
 * Attempts to convert the provided value by calling the expected type's
 * constructor. If the conversion is successful and the result is an instance
 * of the expected type, the converted object is returned.
 *
 * @param value The Python object to convert.
 * @param ts Pointer to the TypeSchema describing the expected type.
 * @param collector Pointer to an ErrorCollector for reporting errors.
 * @param error_path A string representing the path for error messages.
 * @param deserializers Pointer to registered deserializers.
 * @return A new PyObject on success, or nullptr on error.
 */
static PyObject *convert_using_constructor(PyObject *value, TypeSchema *ts,
                                           ErrorCollector *collector,
                                           const char *error_path,
                                           Deserializers *deserializers) {
  PyObject *conv =
      PyObject_CallFunctionObjArgs(ts->expected_type, value, nullptr);
  if (conv && PyObject_IsInstance(conv, ts->expected_type)) {
    return conv;
  }
  Py_XDECREF(conv);
  PyErr_Clear();
  if (collector) {
    collector->add_error(error_path, std::string("Expected type ") +
                                         safe_type_name(ts->expected_type) +
                                         ", got " + safe_type_name(value));
  }
  return nullptr;
}

/**
 * @brief Validates and converts a Python object to the expected type.
 *
 * This function handles conversion for optional types, data models, containers,
 * unions, and plain types by dispatching to appropriate helper functions.
 *
 * @param value The Python object to validate.
 * @param ts Pointer to the TypeSchema describing the expected type.
 * @param collector Pointer to an ErrorCollector for reporting errors.
 * @param error_path A string representing the path for error messages.
 * @param deserializers Pointer to registered deserializers.
 * @return A new PyObject on success, or nullptr on error.
 */
PyObject *validate_and_convert(PyObject *value, TypeSchema *ts,
                               ErrorCollector *collector,
                               const char *error_path,
                               Deserializers *deserializers) {
  if (value == Py_None) {
    if (ts->is_optional) {
      Py_INCREF(Py_None);
      return Py_None;
    }
  }

  if (ts->expected_type == AnyType) {
    Py_INCREF(value);
    return value;
  }

  if (ts->is_data_model && PyDict_Check(value)) {
    return validate_data_model(value, ts, collector, error_path, deserializers);
  }

  switch (ts->container_kind) {
  case CK_LIST:
    return validate_list(value, ts, collector, error_path, deserializers);
  case CK_DICT:
    return validate_dict(value, ts, collector, error_path, deserializers);
  case CK_TUPLE:
    return validate_tuple(value, ts, collector, error_path, deserializers);
  case CK_SET:
    return validate_set(value, ts, collector, error_path, deserializers);
  }

  if (ts->origin == Py_None) {
    return validate_plain(value, ts, collector, error_path, deserializers);
  }

  if (ts->container_kind == CK_UNION) {
    return validate_union(value, ts, collector, error_path, deserializers);
  }

  return convert_using_constructor(value, ts, collector, error_path,
                                   deserializers);
}

/**
 * @brief Initializes the validation globals for the extension.
 *
 * This function initializes the extension globals and the AnyType.
 *
 * @return 0 on success, -1 on failure.
 */
int init_validation_globals(void) {
  if (init_extension_globals() != 0) {
    return -1;
  }
  return init_any_type();
}
