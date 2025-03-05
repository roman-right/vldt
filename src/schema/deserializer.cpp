#include "deserializer.hpp"

/**
 * @brief Create a Deserializers structure from a Python dict.
 * @param deserializer_dict A Python dictionary containing deserializer
 * functions.
 * @return Deserializers* Pointer to the created Deserializers structure.
 */
Deserializers *create_deserializers(PyObject *deserializer_dict) {
  if (!deserializer_dict || !PyDict_Check(deserializer_dict)) {
    PyErr_SetString(PyExc_TypeError, "deserializer_dict must be a dict");
    return nullptr;
  }

  Deserializers *deserializers = new Deserializers();
  if (!deserializers) {
    PyErr_NoMemory();
    return nullptr;
  }

  PyObject *outer_key, *outer_value;
  Py_ssize_t pos = 0;
  while (PyDict_Next(deserializer_dict, &pos, &outer_key, &outer_value)) {
    if (!PyDict_Check(outer_value)) {
      PyErr_SetString(PyExc_TypeError,
                      "Each value in deserializer_dict must be a dict");
      free_deserializers(deserializers);
      return nullptr;
    }
    PyObject *inner_key, *inner_value;
    Py_ssize_t inner_pos = 0;
    while (PyDict_Next(outer_value, &inner_pos, &inner_key, &inner_value)) {
      if (!PyCallable_Check(inner_value)) {
        PyErr_SetString(PyExc_TypeError,
                        "Deserializer function must be callable");
        free_deserializers(deserializers);
        return nullptr;
      }
      Py_INCREF(outer_key);
      Py_INCREF(inner_key);
      Py_INCREF(inner_value);
      DeserializerKey dk = {outer_key, inner_key};
      deserializers->map.insert({dk, inner_value});
    }
  }
  return deserializers;
}

/**
 * @brief Retrieve the cached deserializer function for the given types.
 * @param deserializers Pointer to the Deserializers structure.
 * @param deserialize_to The target type.
 * @param deserialize_from The source type.
 * @return PyObject* The cached deserializer function or Py_None.
 */
PyObject *get_deserializer(Deserializers *deserializers,
                           PyObject *deserialize_to,
                           PyObject *deserialize_from) {
  if (!deserializers) {
    PyErr_SetString(PyExc_RuntimeError, "Deserializers structure is null");
    return nullptr;
  }
  DeserializerKey dk = {deserialize_to, deserialize_from};
  auto it = deserializers->map.find(dk);
  if (it != deserializers->map.end()) {
    Py_INCREF(it->second);
    return it->second;
  }
  Py_RETURN_NONE;
}

/**
 * @brief Free the Deserializers structure and decrement reference counts on all
 * stored objects.
 * @param deserializers Pointer to the Deserializers structure to be freed.
 */
void free_deserializers(Deserializers *deserializers) {
  if (!deserializers) {
    return;
  }
  for (auto &entry : deserializers->map) {
    Py_DECREF(entry.first.deserialize_to);
    Py_DECREF(entry.first.deserialize_from);
    Py_DECREF(entry.second);
  }
  delete deserializers;
}
