#pragma once

#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Global objects shared across modules.
 */
extern PyObject *empty_tuple;
extern PyObject *ClassVarType;
extern PyObject *AnyType;
extern PyObject *UnionType;
extern PyObject *generic_cache;
extern PyObject *TupleType;
extern PyObject *SetType;
extern PyObject *DictType;
extern PyObject *ListType;

/**
 * @brief Primitive types.
 */
extern PyObject *IntType;
extern PyObject *FloatType;
extern PyObject *StrType;
extern PyObject *BoolType;
extern PyObject *NoneType;

/**
 * @brief Sentinel for undefined default.
 */
extern PyObject *VLDTUndefined;

/**
 * @brief Functions to initialize individual globals.
 */
int init_empty_tuple();
int init_class_var_type();
int init_any_type();
int ensure_union_type();
int ensure_generic_cache();

/**
 * @brief Initialize the undefined default sentinel.
 */
int init_vldt_undefined();

/**
 * @brief Combined global initialization for the extension.
 */
int init_extension_globals(void);

#ifdef __cplusplus
}
#endif
