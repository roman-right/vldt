#pragma once

#include <Python.h>

/**
 * @brief Convert a DataModel instance to a dictionary.
 *
 * This function converts a DataModel instance into a dictionary representation.
 *
 * @param self The DataModel instance.
 * @param unused Unused parameter.
 * @return PyObject* The resulting dictionary, or nullptr on error.
 */
PyObject *dict_utils_to_dict(PyObject *self, PyObject *unused);

/**
 * @brief Create a DataModel instance from a dictionary.
 *
 * This function creates a new DataModel instance by using the given dictionary.
 *
 * @param cls The class object to instantiate.
 * @param args Arguments tuple containing the dictionary.
 * @return PyObject* The new DataModel instance, or nullptr on error.
 */
PyObject *dict_utils_from_dict(PyObject *cls, PyObject *args);
