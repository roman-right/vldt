#pragma once

#include "error_handling.hpp"
#include "schema/deserializer.hpp" // include deserializers header
#include "schema/schema.hpp"
#include <Python.h>

/**
 * @brief Validate and convert a Python value according to the provided type
 * schema.
 *
 * Any errors encountered during validation are recorded into the provided
 * ErrorCollector.
 *
 * @param value The input Python value to validate/convert.
 * @param ts Pointer to the compiled type schema against which to validate.
 * @param collector Pointer to an ErrorCollector for recording any validation
 * errors.
 * @param error_path A string describing the current location in the data
 * structure for error reporting.
 * @param deserializers Pointer to a Deserializers cache (from the model
 * configuration) used to convert between types.
 * @return A new reference to the validated/converted value on success, or
 * nullptr if validation fails.
 */
PyObject *validate_and_convert(PyObject *value, TypeSchema *ts,
                               ErrorCollector *collector,
                               const char *error_path,
                               Deserializers *deserializers);

/**
 * @brief Initialize validation globals.
 *
 * @return 0 on success; -1 on error.
 */
int init_validation_globals(void);
