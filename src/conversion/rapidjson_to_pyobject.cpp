#ifdef _MSC_VER
#define __builtin_expect(exp, c) (exp)
#endif

#include "rapidjson_to_pyobject.hpp"
#include <Python.h>
#include <rapidjson/document.h>

/**
 * @brief Converts a rapidjson::Value to a corresponding PyObject.
 *
 * @param value A constant reference to a rapidjson::Value.
 * @return PyObject* The corresponding PyObject representation of the value.
 */
PyObject *rapidjson_to_pyobject(const rapidjson::Value &value) {
  try {
    if (__builtin_expect(value.IsNull(), 0)) {
      Py_INCREF(Py_None);
      return Py_None;
    }
    if (value.IsBool()) {
      return PyBool_FromLong(value.GetBool());
    }
    if (value.IsInt()) {
      return PyLong_FromLong(value.GetInt());
    }
    if (value.IsInt64()) {
      return PyLong_FromLongLong(value.GetInt64());
    }
    if (value.IsUint()) {
      return PyLong_FromUnsignedLong(value.GetUint());
    }
    if (value.IsUint64()) {
      return PyLong_FromUnsignedLongLong(value.GetUint64());
    }
    if (value.IsDouble()) {
      return PyFloat_FromDouble(value.GetDouble());
    }
    if (value.IsString()) {
      return PyUnicode_FromStringAndSize(value.GetString(),
                                         value.GetStringLength());
    }
    if (value.IsArray()) {
      PyObject *list_obj = PyList_New(value.Size());
      if (!list_obj) {
        return nullptr;
      }
      for (rapidjson::SizeType i = 0; i < value.Size(); i++) {
        PyObject *item = rapidjson_to_pyobject(value[i]);
        if (!item) {
          Py_DECREF(list_obj);
          return nullptr;
        }
        PyList_SET_ITEM(list_obj, i, item);
      }
      return list_obj;
    }
    if (value.IsObject()) {
      PyObject *dict_obj = PyDict_New();
      if (!dict_obj) {
        return nullptr;
      }
      for (auto itr = value.MemberBegin(); itr != value.MemberEnd(); ++itr) {
        PyObject *key_obj = PyUnicode_FromStringAndSize(
            itr->name.GetString(), itr->name.GetStringLength());
        if (!key_obj) {
          Py_DECREF(dict_obj);
          return nullptr;
        }
        PyObject *val_obj = rapidjson_to_pyobject(itr->value);
        if (!val_obj) {
          Py_DECREF(key_obj);
          Py_DECREF(dict_obj);
          return nullptr;
        }
        if (PyDict_SetItem(dict_obj, key_obj, val_obj) < 0) {
          Py_DECREF(key_obj);
          Py_DECREF(val_obj);
          Py_DECREF(dict_obj);
          return nullptr;
        }
        Py_DECREF(key_obj);
        Py_DECREF(val_obj);
      }
      return dict_obj;
    }
    PyErr_SetString(PyExc_TypeError, "Unknown rapidjson value type");
    return nullptr;
  } catch (const std::exception &e) {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
}
