#include "validation_containers.hpp"
#include "error_handling.hpp"
#include "init_globals.hpp"
#include "safe_type_name.hpp"
#include "schema/deserializer.hpp"
#include "schema/schema.hpp"
#include "validation.hpp"
#include "validation_primitives.hpp"
#include <Python.h>
#include <stdio.h>
#include <string>

/**
 * @brief Validates and converts a Python list.
 *
 * Checks if the given value is a list and converts each element using
 * validate_and_convert.
 *
 * @param value The Python object to validate.
 * @param ts The type schema for the list elements.
 * @param collector The error collector.
 * @param error_path The base error path.
 * @param deserializers The deserializers container.
 * @return A new list with validated and converted items, or nullptr on error.
 */
PyObject *validate_list(PyObject *value, TypeSchema *ts,
                        ErrorCollector *collector, const char *error_path,
                        Deserializers *deserializers) {
  if (!PyList_Check(value)) {
    if (collector) {
      collector->add_error(error_path, std::string("Expected a list, got ") +
                                           safe_type_name(value));
    }
    return nullptr;
  }
  Py_ssize_t size = PyList_Size(value);
  PyObject *new_list = PyList_New(size);
  if (!new_list) {
    return nullptr;
  }

  std::string new_path = std::string(error_path) + ".";

  for (Py_ssize_t i = 0; i < size; i++) {
    PyObject *item = PyList_GetItem(value, i);
    std::string elem_path = new_path + std::to_string(i);
    PyObject *conv_item = validate_and_convert(item, ts->args[0], collector,
                                               elem_path.c_str(), deserializers);
    if (!conv_item) {
      Py_DECREF(new_list);
      return nullptr;
    }
    PyList_SET_ITEM(new_list, i, conv_item);
  }
  return new_list;
}

/**
 * @brief Validates and converts a Python dictionary.
 *
 * Checks if the given value is a dict and converts each key-value pair using
 * validate_and_convert.
 *
 * @param value The Python object to validate.
 * @param ts The type schema for the dictionary keys and values.
 * @param collector The error collector.
 * @param error_path The base error path.
 * @param deserializers The deserializers container.
 * @return A new dictionary with validated and converted key-value pairs, or
 * nullptr on error.
 */
PyObject *validate_dict(PyObject *value, TypeSchema *ts,
                        ErrorCollector *collector, const char *error_path,
                        Deserializers *deserializers) {
  if (!PyDict_Check(value)) {
    if (collector) {
      collector->add_error(error_path, std::string("Expected a dict, got ") +
                                           safe_type_name(value));
    }
    return nullptr;
  }
  PyObject *new_dict = PyDict_New();
  if (!new_dict) {
    return nullptr;
  }

  TypeSchema *key_schema = ts->args[0];
  TypeSchema *val_schema = ts->args[1];

  std::string base_path = std::string(error_path) + ".";

  PyObject *key, *val;
  Py_ssize_t pos = 0;
  while (PyDict_Next(value, &pos, &key, &val)) {
    const char *key_str =
        PyUnicode_Check(key) ? PyUnicode_AsUTF8(key) : safe_type_name(key);
    std::string elem_path = base_path + key_str;
    PyObject *conv_key = validate_and_convert(key, key_schema, collector,
                                              elem_path.c_str(), deserializers);
    if (!conv_key) {
      Py_DECREF(new_dict);
      return nullptr;
    }
    PyObject *conv_val = validate_and_convert(val, val_schema, collector,
                                              elem_path.c_str(), deserializers);
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
 * @brief Validates and converts a Python tuple.
 *
 * Checks if the given value is a tuple with the expected length and converts
 * each element using validate_and_convert.
 *
 * @param value The Python object to validate.
 * @param ts The type schema for the tuple elements.
 * @param collector The error collector.
 * @param error_path The base error path.
 * @param deserializers The deserializers container.
 * @return A new tuple with validated and converted items, or nullptr on error.
 */
PyObject *validate_tuple(PyObject *value, TypeSchema *ts,
                         ErrorCollector *collector, const char *error_path,
                         Deserializers *deserializers) {
  if (!PyTuple_Check(value)) {
    if (collector) {
      collector->add_error(error_path, std::string("Expected a tuple, got ") +
                                           safe_type_name(value));
    }
    return nullptr;
  }
  Py_ssize_t size = PyTuple_Size(value);
  // Tuple[T, ...]: any length, every element of type T (args[0]).
  if (ts->is_variadic_tuple) {
    PyObject *new_tuple = PyTuple_New(size);
    if (!new_tuple) {
      return nullptr;
    }
    TypeSchema *element_ts = ts->args[0];
    for (Py_ssize_t i = 0; i < size; i++) {
      PyObject *item = PyTuple_GetItem(value, i);
      std::string new_path = std::string(error_path) + "." + std::to_string(i);
      PyObject *conv_item = validate_and_convert(
          item, element_ts, collector, new_path.c_str(), deserializers);
      if (!conv_item) {
        Py_DECREF(new_tuple);
        return nullptr;
      }
      PyTuple_SET_ITEM(new_tuple, i, conv_item);
    }
    return new_tuple;
  }
  if (ts->num_args != size) {
    if (collector) {
      std::string msg = "Expected tuple of length " + std::to_string(ts->num_args) + ", got " + std::to_string(size);
      collector->add_error(error_path, msg);
    }
    return nullptr;
  }
  PyObject *new_tuple = PyTuple_New(size);
  if (!new_tuple) {
    return nullptr;
  }
  for (Py_ssize_t i = 0; i < size; i++) {
    PyObject *item = PyTuple_GetItem(value, i);
    std::string new_path = std::string(error_path) + "." + std::to_string(i);
    PyObject *conv_item = validate_and_convert(item, ts->args[i], collector,
                                               new_path.c_str(), deserializers);
    if (!conv_item) {
      Py_DECREF(new_tuple);
      return nullptr;
    }
    PyTuple_SET_ITEM(new_tuple, i, conv_item);
  }
  return new_tuple;
}

/**
 * @brief Validates and converts a Python set.
 *
 * Checks if the given value is a set and converts each element using
 * validate_and_convert.
 *
 * @param value The Python object to validate.
 * @param ts The type schema for the set elements.
 * @param collector The error collector.
 * @param error_path The base error path.
 * @param deserializers The deserializers container.
 * @return A new set with validated and converted items, or nullptr on error.
 */
PyObject *validate_set(PyObject *value, TypeSchema *ts,
                       ErrorCollector *collector, const char *error_path,
                       Deserializers *deserializers) {
  if (!PySet_Check(value)) {
    if (collector) {
      collector->add_error(error_path, std::string("Expected a set, got ") +
                                           safe_type_name(value));
    }
    return nullptr;
  }
  PyObject *new_set = PySet_New(nullptr);
  if (!new_set) {
    return nullptr;
  }
  PyObject *iterator = PyObject_GetIter(value);
  if (!iterator) {
    Py_DECREF(new_set);
    return nullptr;
  }
  PyObject *item;
  Py_ssize_t idx = 0;
  while ((item = PyIter_Next(iterator)) != nullptr) {
    std::string new_path = std::string(error_path) + "." + std::to_string(idx++);
    PyObject *conv_item = validate_and_convert(item, ts->args[0], collector,
                                                new_path.c_str(), deserializers);
    Py_DECREF(item);
    if (!conv_item) {
      Py_DECREF(iterator);
      Py_DECREF(new_set);
      return nullptr;
    }
    if (PySet_Add(new_set, conv_item) < 0) {
      Py_DECREF(conv_item);
      Py_DECREF(iterator);
      Py_DECREF(new_set);
      return nullptr;
    }
    Py_DECREF(conv_item);
  }
  Py_DECREF(iterator);
  return new_set;
}

/**
 * @brief Validates and converts a Python object for a Union type.
 *
 * First checks if the value is already an instance of any candidate type.
 * If not, attempts conversion for each candidate. Returns the converted value
 * if successful, or logs an error if all candidates fail.
 *
 * @param value The Python object to validate.
 * @param ts The type schema for the union.
 * @param collector The error collector.
 * @param error_path The base error path.
 * @param deserializers The deserializers container.
 * @return The validated and converted Python object, or nullptr on error.
 */
PyObject *validate_union(PyObject *value, TypeSchema *ts,
                         ErrorCollector *collector, const char *error_path,
                         Deserializers *deserializers) {
  bool value_is_bool = PyBool_Check(value);
  for (Py_ssize_t i = 0; i < ts->num_args; i++) {
    TypeSchema *candidate = ts->args[i];
    // typing.Literal candidates need value-equality, not isinstance, and
    // typing.Literal isn't a real class so PyObject_IsInstance would
    // error. Defer to the second pass which dispatches into
    // validate_literal via validate_and_convert.
    if (candidate->is_literal) {
      continue;
    }
    PyObject *check_type = (candidate->origin != Py_None)
                               ? candidate->origin
                               : candidate->expected_type;
    // Bool is a subclass of int; an `int` arm in a Union must not silently
    // swallow True/False (matches the asymmetry fix in validate_plain).
    if (value_is_bool && check_type == IntType) {
      continue;
    }
    int is_inst = PyObject_IsInstance(value, check_type);
    if (is_inst < 0) {
      // The candidate type cannot be tested with isinstance (e.g. a typing
      // special form). Fall through to the slow path.
      PyErr_Clear();
      continue;
    }
    if (is_inst) {
      Py_INCREF(value);
      return value;
    }
  }
  ErrorCollector temp_collector;
  for (Py_ssize_t i = 0; i < ts->num_args; i++) {
    PyObject *conv = validate_and_convert(value, ts->args[i], &temp_collector,
                                          error_path, deserializers);
    if (conv) {
      return conv;
    }
    PyErr_Clear();
  }
  if (collector) {
    collector->add_error(
        error_path,
        std::string("Value did not match any candidate in Union: got ") +
            safe_type_name(value));
  }
  return nullptr;
}
