#pragma once

#include <Python.h>
#include <rapidjson/document.h>

/**
 * @brief Converts a rapidjson::Value to a corresponding PyObject.
 *
 * @param value A constant reference to a rapidjson::Value.
 * @return PyObject* A new reference on success, or nullptr on failure.
 */
PyObject *rapidjson_to_pyobject(const rapidjson::Value &value);
