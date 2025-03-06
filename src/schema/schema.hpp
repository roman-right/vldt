#pragma once

#include "schema/deserializer.hpp" // Include the deserializers header
#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Container kind definitions.
 *
 * CK_NONE (0): not a container
 * CK_DICT (1): dict
 * CK_LIST (2): list
 * CK_TUPLE (3): tuple
 * CK_SET (4): set
 * CK_UNION (5): union
 */
enum ContainerKind {
  CK_NONE = 0,
  CK_DICT = 1,
  CK_LIST = 2,
  CK_TUPLE = 3,
  CK_SET = 4,
  CK_UNION = 5
};

/**
 * @brief A structure that caches generic type information.
 *
 * This unified schema holds:
 *  - The original expected type (e.g. int, List[str], etc.)
 *  - The cached __origin__ attribute (or Py_None if not generic).
 *  - The number of type arguments (from __args__).
 *  - An array of pointers to the TypeSchema for each type argument.
 *  - A cached representation of the type (for error messages).
 *  - The cached UTF-8 representation of the type’s repr.
 *  - Flags for model and optional types.
 *  - Container information: container_kind and, if applicable,
 *    inner_model_type.
 */
struct TypeSchema {
  PyObject *expected_type;
  PyObject *origin;
  Py_ssize_t num_args;
  struct TypeSchema **args;
  PyObject *repr;
  const char *utf8_repr;
  int is_data_model;
  int is_optional;
  int cached;
  int container_kind;
  PyObject *inner_model_type;
};

/**
 * @brief Structure for field metadata.
 *
 * Contains per-field information and a pointer to the unified TypeSchema.
 *
 * Note: Fields no longer carry type details; these now reside solely in
 * TypeSchema.
 */
struct FieldSchema {
  PyObject *field_name;
  const char *field_name_c;
  PyObject *alias;
  PyObject *default_value;
  PyObject *default_factory;
  TypeSchema *type_schema;
};

/**
 * @brief Structure aggregating model-level schema information.
 *
 * Contains an array of FieldSchema entries plus any configuration/validator
 * settings.
 */
struct SchemaCache {
  FieldSchema *fields;
  Py_ssize_t num_fields;
  PyObject *config;
  PyObject *dict_serializer;
  PyObject *json_serializer;
  PyObject *instance_annotations;
  PyObject *validators;
  PyObject *cached_to_dict;
  int has_field_before;
  int has_field_after;
  int has_model_before;
  int has_model_after;
  Deserializers *deserializers;
};

/**
 * @brief Recursively compiles a TypeSchema from an expected type.
 *
 * @param expected_type The Python type object to compile.
 * @return A pointer to a newly allocated TypeSchema, or nullptr on error.
 */
TypeSchema *compile_type_schema(PyObject *expected_type);

/**
 * @brief Recursively frees a TypeSchema (unless it's cached).
 *
 * @param ts Pointer to the TypeSchema to free.
 */
void free_type_schema(TypeSchema *ts);

/**
 * @brief Retrieves a cached SchemaCache for the given model class.
 *
 * If the schema is not cached on the class, it is compiled.
 *
 * @param cls The model class (a Python type).
 * @return A new reference to the PyCapsule, or nullptr on error.
 */
PyObject *get_schema_cached(PyObject *cls);

#ifdef __cplusplus
}
#endif
