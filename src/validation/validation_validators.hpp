#pragma once

#include "schema/schema.hpp" // For SchemaCache
#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run a list of model-level validators.
 * If call_with_cls is true, each validator is called with (cls, target);
 * otherwise, with (target,).
 *
 * @param cls            The model class.
 * @param validator_list A Python list of validators.
 * @param target         The dictionary or model instance to validate.
 * @param call_with_cls  Flag to control the calling signature.
 * @return 0 on success, -1 on failure.
 */
int run_model_validators(PyObject *cls, PyObject *validator_list,
                         PyObject *target, int call_with_cls);

/**
 * Run field BEFORE validators.
 *
 * @param schema A pointer to the compiled SchemaCache.
 * @param cls    The model class.
 * @param pKwds  Pointer to the dict containing field values.
 * @return 0 on success, -1 on failure.
 */
int run_field_before_validators(SchemaCache *schema, PyObject *cls,
                                PyObject **pKwds);

/**
 * Run model BEFORE validators.
 *
 * @param schema A pointer to the compiled SchemaCache.
 * @param cls    The model class.
 * @param pKwds  Pointer to the dict containing field values.
 * @return 0 on success, -1 on failure.
 */
int run_model_before_validators(SchemaCache *schema, PyObject *cls,
                                PyObject **pKwds);

/**
 * Run field AFTER validators.
 *
 * @param schema A pointer to the compiled SchemaCache.
 * @param cls    The model class.
 * @param self   The model instance.
 * @return 0 on success, -1 on failure.
 */
int run_field_after_validators(SchemaCache *schema, PyObject *cls,
                               PyObject *self);

/**
 * Run model AFTER validators.
 *
 * @param schema A pointer to the compiled SchemaCache.
 * @param cls    The model class.
 * @param self   The model instance.
 * @return 0 on success, -1 on failure.
 */
int run_model_after_validators(SchemaCache *schema, PyObject *cls,
                               PyObject *self);

/**
 * Run a list of field validators on a single value.
 *
 * Each validator is called as `validator(cls, value)` and the result becomes
 * the next validator's input. The returned value is a new reference; the
 * caller is responsible for releasing it. The input `value` ref is consumed.
 * Returns nullptr on the first validator that raises (with the Python error
 * indicator set).
 *
 * @param cls           The model class.
 * @param validators    A Python list of validator callables/classmethods.
 * @param value         New reference to the value being validated. Consumed.
 * @return New reference to the final validated value, or nullptr on error.
 */
PyObject *run_field_validators_on_value(PyObject *cls, PyObject *validators,
                                        PyObject *value);

#ifdef __cplusplus
}
#endif
