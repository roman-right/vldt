#pragma once

#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Forward declaration of TypeSchema.
 */
struct TypeSchema;

/**
 * @brief Forward declaration of ErrorCollector.
 */
class ErrorCollector;

/**
 * @brief Forward declaration of Deserializers.
 */
struct Deserializers;

/**
 * @brief Validate a Python list by converting each element.
 *
 * @param value The Python list to validate.
 * @param ts The type schema for validation.
 * @param collector ErrorCollector for recording errors.
 * @param error_path Path string for error messages.
 * @param deserializers Deserializers for conversion.
 * @return A new PyObject after validation, or nullptr on error.
 */
PyObject *validate_list(PyObject *value, TypeSchema *ts,
                        ErrorCollector *collector, const char *error_path,
                        Deserializers *deserializers);

/**
 * @brief Validate a Python dictionary by converting each key/value pair.
 *
 * @param value The Python dictionary to validate.
 * @param ts The type schema for validation.
 * @param collector ErrorCollector for recording errors.
 * @param error_path Path string for error messages.
 * @param deserializers Deserializers for conversion.
 * @return A new PyObject after validation, or nullptr on error.
 */
PyObject *validate_dict(PyObject *value, TypeSchema *ts,
                        ErrorCollector *collector, const char *error_path,
                        Deserializers *deserializers);

/**
 * @brief Validate a Python tuple by converting each element.
 *
 * @param value The Python tuple to validate.
 * @param ts The type schema for validation.
 * @param collector ErrorCollector for recording errors.
 * @param error_path Path string for error messages.
 * @param deserializers Deserializers for conversion.
 * @return A new PyObject after validation, or nullptr on error.
 */
PyObject *validate_tuple(PyObject *value, TypeSchema *ts,
                         ErrorCollector *collector, const char *error_path,
                         Deserializers *deserializers);

/**
 * @brief Validate a Python set by converting each element.
 *
 * @param value The Python set to validate.
 * @param ts The type schema for validation.
 * @param collector ErrorCollector for recording errors.
 * @param error_path Path string for error messages.
 * @param deserializers Deserializers for conversion.
 * @return A new PyObject after validation, or nullptr on error.
 */
PyObject *validate_set(PyObject *value, TypeSchema *ts,
                       ErrorCollector *collector, const char *error_path,
                       Deserializers *deserializers);

/**
 * @brief Validate a union type by testing each candidate.
 *
 * @param value The Python object to validate.
 * @param ts The union type schema for validation.
 * @param collector ErrorCollector for recording errors.
 * @param error_path Path string for error messages.
 * @param deserializers Deserializers for conversion.
 * @return A new PyObject after validation, or nullptr on error.
 */
PyObject *validate_union(PyObject *value, TypeSchema *ts,
                         ErrorCollector *collector, const char *error_path,
                         Deserializers *deserializers);

#ifdef __cplusplus
}
#endif
