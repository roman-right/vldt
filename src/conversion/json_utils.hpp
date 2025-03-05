#pragma once

#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a DataModel instance from a JSON string.
 *
 * This function converts a JSON string into a new DataModel instance.
 * It expects exactly one argument (a JSON string) and returns the instance
 * or NULL on failure.
 *
 * @param cls The Python type object for the DataModel.
 * @param args Pointer to an array of Python objects (arguments).
 * @param nargs Number of arguments provided.
 * @return A new DataModel instance on success, or NULL on error.
 */
PyObject *json_utils_from_json(PyObject *cls, PyObject *const *args,
                               Py_ssize_t nargs);

/**
 * @brief Convert a DataModel instance to a JSON string.
 *
 * This function converts a DataModel instance into a JSON string using a custom
 * JSON serializer from the associated schema. The resulting JSON string is
 * returned as a Python Unicode object.
 *
 * @param self The DataModel instance.
 * @param Py_UNUSED(ignored) Unused parameter.
 * @return A Python Unicode object containing the JSON string, or NULL on error.
 */
PyObject *json_utils_to_json(PyObject *self, PyObject *Py_UNUSED(ignored));

#ifdef __cplusplus
}
#endif
