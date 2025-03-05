#include "validation_containers.hpp"
#include "error_handling.hpp"
#include "init_globals.hpp"
#include "schema/deserializer.hpp"
#include "schema/schema.hpp"
#include "validation.hpp"
#include "validation_primitives.hpp"
#include <Python.h>
#include <array>
#include <stdio.h>
#include <string>

/**
 * @brief Returns a safe type name for a Python object.
 *
 * If the object is a type, returns its __name__ or __qualname__ attribute.
 * Otherwise, returns the type's name or "<unknown>" if unavailable.
 *
 * @param obj The Python object.
 * @return A C-string representing the safe type name.
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

  size_t base_len = strlen(error_path);
  std::array<char, 256> new_path;
  if (base_len >= new_path.size() - 2) {
    base_len = new_path.size() - 2;
  }
  memcpy(new_path.data(), error_path, base_len);
  new_path[base_len] = '.';
  new_path[base_len + 1] = '\0';

  for (Py_ssize_t i = 0; i < size; i++) {
    PyObject *item = PyList_GetItem(value, i);
    snprintf(new_path.data() + base_len + 1, new_path.size() - base_len - 1,
             "%zd", i);
    PyObject *conv_item = validate_and_convert(item, ts->args[0], collector,
                                               new_path.data(), deserializers);
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

  size_t base_len = strlen(error_path);
  std::array<char, 256> new_path;
  if (base_len >= new_path.size() - 2) {
    base_len = new_path.size() - 2;
  }
  memcpy(new_path.data(), error_path, base_len);
  new_path[base_len] = '.';
  new_path[base_len + 1] = '\0';

  PyObject *key, *val;
  Py_ssize_t pos = 0;
  while (PyDict_Next(value, &pos, &key, &val)) {
    const char *key_str =
        PyUnicode_Check(key) ? PyUnicode_AsUTF8(key) : safe_type_name(key);
    snprintf(new_path.data() + base_len + 1, new_path.size() - base_len - 1,
             "%s", key_str);
    PyObject *conv_key = validate_and_convert(key, key_schema, collector,
                                              new_path.data(), deserializers);
    if (!conv_key) {
      Py_DECREF(new_dict);
      return nullptr;
    }
    PyObject *conv_val = validate_and_convert(val, val_schema, collector,
                                              new_path.data(), deserializers);
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
  if (ts->num_args != size) {
    if (collector) {
      std::array<char, 128> buf;
      snprintf(buf.data(), buf.size(), "Expected tuple of length %zd, got %zd",
               ts->num_args, size);
      collector->add_error(error_path, buf.data());
    }
    return nullptr;
  }
  PyObject *new_tuple = PyTuple_New(size);
  if (!new_tuple) {
    return nullptr;
  }
  for (Py_ssize_t i = 0; i < size; i++) {
    PyObject *item = PyTuple_GetItem(value, i);
    std::array<char, 256> new_path;
    snprintf(new_path.data(), new_path.size(), "%s.%zd", error_path, i);
    PyObject *conv_item = validate_and_convert(item, ts->args[i], collector,
                                               new_path.data(), deserializers);
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
    std::array<char, 256> new_path;
    snprintf(new_path.data(), new_path.size(), "%s.%zd", error_path, idx++);
    PyObject *conv_item = validate_and_convert(item, ts->args[0], collector,
                                               new_path.data(), deserializers);
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
  for (Py_ssize_t i = 0; i < ts->num_args; i++) {
    TypeSchema *candidate = ts->args[i];
    PyObject *check_type = (candidate->origin != Py_None)
                               ? candidate->origin
                               : candidate->expected_type;
    if (PyObject_IsInstance(value, check_type)) {
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
