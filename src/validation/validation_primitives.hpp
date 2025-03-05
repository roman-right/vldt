#ifndef VALIDATION_PRIMITIVES_HPP
#define VALIDATION_PRIMITIVES_HPP

#include "error_handling.hpp"
#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Validate and possibly convert to an int primitive.
 *
 * If the value is already an int, it is returned with a new reference.
 * Otherwise, an attempt is made to convert it via the int() constructor.
 *
 * @param value The Python object to validate.
 * @param collector Optional error collector for reporting errors.
 * @param error_path Path to include in any error messages.
 * @return New reference to a valid int object on success; nullptr on failure.
 */
PyObject *validate_int(PyObject *value, ErrorCollector *collector,
                       const char *error_path);

/**
 * @brief Validate and possibly convert to a string primitive.
 *
 * @param value The Python object to validate.
 * @param collector Optional error collector for reporting errors.
 * @param error_path Path to include in any error messages.
 * @return New reference to a valid string object on success; nullptr on
 * failure.
 */
PyObject *validate_str(PyObject *value, ErrorCollector *collector,
                       const char *error_path);

/**
 * @brief Validate and possibly convert to a float primitive.
 *
 * @param value The Python object to validate.
 * @param collector Optional error collector for reporting errors.
 * @param error_path Path to include in any error messages.
 * @return New reference to a valid float object on success; nullptr on failure.
 */
PyObject *validate_float(PyObject *value, ErrorCollector *collector,
                         const char *error_path);

/**
 * @brief Validate and possibly convert to a bool primitive.
 *
 * @param value The Python object to validate.
 * @param collector Optional error collector for reporting errors.
 * @param error_path Path to include in any error messages.
 * @return New reference to a valid bool object on success; nullptr on failure.
 */
PyObject *validate_bool(PyObject *value, ErrorCollector *collector,
                        const char *error_path);

#ifdef __cplusplus
}
#endif

#endif // VALIDATION_PRIMITIVES_HPP
