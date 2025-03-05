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

#ifdef __cplusplus
}
#endif
