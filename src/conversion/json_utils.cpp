#include "json_utils.hpp"
#include "conversion/rapidjson_to_pyobject.hpp"
#include "data_model.hpp"
#include "init_globals.hpp"
#include "schema/schema.hpp"
#include <Python.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <unordered_map>
#include <vector>

// Forward declaration.
static bool
write_json_value(PyObject *value, PyObject *json_serializer,
                 rapidjson::Writer<rapidjson::StringBuffer> &writer);

// Global declarations.
extern PyObject *schema_key;
extern PyObject *get_schema_cached(PyObject *cls);

/**
 * @brief Retrieve the cached schema from the type dictionary.
 */
static PyObject *get_cached_schema(PyTypeObject *type_ptr) {
  PyObject *type_dict = type_ptr->tp_dict;
  PyObject *capsule = nullptr;
  if (type_dict && PyDict_Check(type_dict)) {
    capsule = PyDict_GetItem(type_dict, schema_key);
    if (capsule) {
      Py_INCREF(capsule);
    }
  }
  if (!capsule) {
    capsule = get_schema_cached(reinterpret_cast<PyObject *>(type_ptr));
    if (capsule && type_dict && PyDict_Check(type_dict)) {
      PyDict_SetItem(type_dict, schema_key, capsule);
    }
  }
  return capsule;
}

/**
 * @brief Recursively write a Python object as JSON using rapidjson.
 *
 * Applies custom conversion from json_serializer when appropriate.
 */
static bool
write_json_value(PyObject *value, PyObject *json_serializer,
                 rapidjson::Writer<rapidjson::StringBuffer> &writer) {
  if (PyObject_TypeCheck(value, &DataModelType)) {
    auto bm = reinterpret_cast<DataModelObject *>(value);
    writer.StartObject();
    for (auto &pair : bm->instance_data->fields) {
      const std::string &field_name = pair.first;
      PyObject *field_value = pair.second;
      writer.Key(field_name.c_str(),
                 static_cast<rapidjson::SizeType>(field_name.size()));
      if (!write_json_value(field_value, json_serializer, writer)) {
        return false;
      }
    }
    writer.EndObject();
    return true;
  } else if (PyList_Check(value)) {
    writer.StartArray();
    Py_ssize_t size = PyList_GET_SIZE(value);
    for (Py_ssize_t i = 0; i < size; i++) {
      PyObject *item = PyList_GET_ITEM(value, i);
      if (!write_json_value(item, json_serializer, writer)) {
        return false;
      }
    }
    writer.EndArray();
    return true;
  } else if (PyDict_Check(value)) {
    writer.StartObject();
    PyObject *key = nullptr;
    PyObject *val = nullptr;
    Py_ssize_t pos = 0;
    while (PyDict_Next(value, &pos, &key, &val)) {
      const char *key_str = nullptr;
      if (PyUnicode_Check(key)) {
        key_str = PyUnicode_AsUTF8(key);
      } else {
        PyObject *key_repr = PyObject_Str(key);
        if (!key_repr) {
          return false;
        }
        key_str = PyUnicode_AsUTF8(key_repr);
        Py_DECREF(key_repr);
      }
      writer.Key(key_str);
      if (!write_json_value(val, json_serializer, writer)) {
        return false;
      }
    }
    writer.EndObject();
    return true;
  } else {
    if (json_serializer && PyDict_Check(json_serializer)) {
      PyObject *type_obj = reinterpret_cast<PyObject *>(Py_TYPE(value));
      PyObject *conv_func = PyDict_GetItem(json_serializer, type_obj);
      if (conv_func && PyCallable_Check(conv_func)) {
        PyObject *converted =
            PyObject_CallFunctionObjArgs(conv_func, value, nullptr);
        if (converted) {
          bool success = write_json_value(converted, json_serializer, writer);
          Py_DECREF(converted);
          return success;
        }
      }
    }
    if (PyBool_Check(value)) {
      writer.Bool(value == Py_True);
      return true;
    } else if (PyLong_Check(value)) {
      long long_val = PyLong_AsLongLong(value);
      writer.Int64(long_val);
      return true;
    } else if (PyFloat_Check(value)) {
      double d = PyFloat_AsDouble(value);
      writer.Double(d);
      return true;
    } else if (PyUnicode_Check(value)) {
      const char *s = PyUnicode_AsUTF8(value);
      writer.String(s);
      return true;
    } else if (value == Py_None) {
      writer.Null();
      return true;
    } else {
      PyObject *str_obj = PyObject_Str(value);
      if (!str_obj) {
        return false;
      }
      const char *s = PyUnicode_AsUTF8(str_obj);
      writer.String(s);
      Py_DECREF(str_obj);
      return true;
    }
  }
}

/**
 * @brief Create a DataModel instance from a JSON string.
 *
 * Parses the JSON string into a Python dictionary and then calls the class's
 * constructor directly using the dictionary as keyword arguments.
 *
 * @param cls Python type.
 * @param json_str JSON string.
 * @return New DataModel instance or nullptr on error.
 */
static PyObject *json_utils_from_json_impl(PyObject *cls,
                                           const char *json_str) {
  if (!json_str || json_str[0] == '\0') {
    PyErr_SetString(PyExc_ValueError, "Empty JSON string");
    return nullptr;
  }
  size_t json_length = std::strlen(json_str);
  std::vector<char> buffer(json_str, json_str + json_length + 1);

  rapidjson::Document doc;
  doc.ParseInsitu(buffer.data());
  if (doc.HasParseError()) {
    PyErr_Format(PyExc_ValueError, "rapidjson parse error: %s (at offset %u)",
                 rapidjson::GetParseError_En(doc.GetParseError()),
                 static_cast<unsigned>(doc.GetErrorOffset()));
    return nullptr;
  }
  if (!doc.IsObject()) {
    PyErr_SetString(PyExc_TypeError, "JSON root must be an object");
    return nullptr;
  }

  PyObject *dict_obj = rapidjson_to_pyobject(doc);
  if (!dict_obj) {
    return nullptr;
  }
  if (!PyDict_Check(dict_obj)) {
    Py_DECREF(dict_obj);
    PyErr_SetString(PyExc_TypeError, "Converted JSON is not a dictionary");
    return nullptr;
  }

  if (!empty_tuple) {
    Py_DECREF(dict_obj);
    return nullptr;
  }
  PyObject *instance = PyObject_Call(cls, empty_tuple, dict_obj);
  Py_DECREF(empty_tuple);
  Py_DECREF(dict_obj);

  return instance;
}

extern "C" {

/**
 * @brief Create a DataModel instance from a JSON string.
 *
 * Expects exactly one argument (a JSON string).
 */
PyObject *json_utils_from_json(PyObject *cls, PyObject *const *args,
                               Py_ssize_t nargs) {
  if (nargs != 1) {
    PyErr_SetString(PyExc_TypeError,
                    "Expected exactly one argument (a JSON string)");
    return nullptr;
  }
  PyObject *json_obj = args[0];
  if (!PyUnicode_Check(json_obj)) {
    PyErr_SetString(PyExc_TypeError, "Argument must be a Unicode string");
    return nullptr;
  }
  const char *json_str = PyUnicode_AsUTF8(json_obj);
  return json_utils_from_json_impl(cls, json_str);
}

/**
 * @brief Convert a DataModel instance to a JSON string.
 *
 * Applies a custom json_serializer and returns a Unicode string.
 */
PyObject *json_utils_to_json(PyObject *self, PyObject *Py_UNUSED(ignored)) {
  PyTypeObject *type_ptr = Py_TYPE(self);
  PyObject *capsule = get_cached_schema(type_ptr);
  if (!capsule) {
    return nullptr;
  }
  SchemaCache *schema = reinterpret_cast<SchemaCache *>(
      PyCapsule_GetPointer(capsule, "vldt.SchemaCache"));
  Py_DECREF(capsule);
  if (!schema) {
    return nullptr;
  }

  PyObject *json_serializer = schema->json_serializer;
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

  if (!write_json_value(self, json_serializer, writer)) {
    PyErr_SetString(PyExc_RuntimeError, "Error converting object to JSON");
    return nullptr;
  }
  return PyUnicode_FromString(sb.GetString());
}

} // extern "C"
