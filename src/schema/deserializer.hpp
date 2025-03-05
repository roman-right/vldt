#pragma once

#include <Python.h>

#ifdef __cplusplus
#include <unordered_map>

/**
 * @brief Key composed of two PyObject* pointers representing the "deserialize
 * to" and "deserialize from" types.
 */
struct DeserializerKey {
  PyObject *deserialize_to;
  PyObject *deserialize_from;

  /**
   * @brief Compare two DeserializerKey objects for equality using pointer
   * equality.
   */
  bool operator==(const DeserializerKey &other) const {
    return deserialize_to == other.deserialize_to &&
           deserialize_from == other.deserialize_from;
  }
};

/**
 * @brief Specialization of std::hash for DeserializerKey.
 */
namespace std {
template <> struct hash<DeserializerKey> {
  std::size_t operator()(const DeserializerKey &k) const {
    return std::hash<PyObject *>()(k.deserialize_to) ^
           (std::hash<PyObject *>()(k.deserialize_from) << 1);
  }
};
} // namespace std

/**
 * @brief Structure that caches deserializer functions.
 */
struct Deserializers {
  std::unordered_map<DeserializerKey, PyObject *> map;
};

extern "C" {
#endif

/**
 * @brief Create a Deserializers structure from a Python dict.
 *
 * The dict must have the structure:
 * {
 *     deserialize_to_type: {
 *         deserialize_from_type: deserializer_function, ...
 *     }, ...
 * }
 *
 * @param deserializer_dict The Python dictionary containing deserializer
 * functions.
 * @return Deserializers* Pointer to the created Deserializers structure.
 */
Deserializers *create_deserializers(PyObject *deserializer_dict);

/**
 * @brief Retrieve the cached deserializer function for the given types.
 *
 * If no function is found, returns Py_None.
 *
 * @param deserializers Pointer to the Deserializers structure.
 * @param deserialize_to The target type.
 * @param deserialize_from The source type.
 * @return PyObject* The cached deserializer function or Py_None.
 */
PyObject *get_deserializer(Deserializers *deserializers,
                           PyObject *deserialize_to,
                           PyObject *deserialize_from);

/**
 * @brief Free the Deserializers structure and decrement reference counts on all
 * stored objects.
 *
 * @param deserializers Pointer to the Deserializers structure to be freed.
 */
void free_deserializers(Deserializers *deserializers);

#ifdef __cplusplus
}
#endif
