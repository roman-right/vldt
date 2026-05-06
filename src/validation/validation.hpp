#pragma once

#include "error_handling.hpp"
#include "schema/deserializer.hpp" // include deserializers header
#include "schema/schema.hpp"
#include <Python.h>
#include <rapidjson/document.h>

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
 * @brief Validate and convert a rapidjson value directly into a Python object.
 *
 * Mirrors validate_and_convert but reads from a rapidjson::Value, avoiding
 * the intermediate Python dict that would otherwise be produced by
 * rapidjson_to_pyobject. For primitive type matches the result is built in
 * one allocation. For coercion paths (custom deserializers, type mismatches)
 * the function materializes the subtree as a Python object and delegates to
 * validate_and_convert so semantics are identical to the dict path.
 */
PyObject *validate_and_convert_from_json(const rapidjson::Value &val,
                                         TypeSchema *ts,
                                         ErrorCollector *collector,
                                         const char *error_path,
                                         Deserializers *deserializers);

/**
 * @brief Construct a DataModel of class `cls` directly from a rapidjson value.
 *
 * Bypasses the intermediate Python dict for the common case (no model_before
 * or field_before validators). When such validators exist, falls back to
 * materializing a Python dict and calling cls(**dict) so semantics are
 * preserved.
 */
PyObject *data_model_from_json(PyObject *cls,
                               const rapidjson::Value &json_obj,
                               ErrorCollector *outer_collector,
                               const char *error_path);

/**
 * @brief Initialize validation globals.
 *
 * @return 0 on success; -1 on error.
 */
int init_validation_globals(void);
