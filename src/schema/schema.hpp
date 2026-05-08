#pragma once

#include "schema/deserializer.hpp" // Include the deserializers header
#include <Python.h>

#ifdef __cplusplus
#include <string>
#include <string_view>
#include <unordered_map>

/**
 * @brief Transparent hasher / equality for the SchemaCache name index.
 *
 * Allows std::unordered_map::find to be called with std::string_view (or a
 * raw const char*) without constructing a temporary std::string per call.
 */
struct SchemaNameHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view sv) const noexcept {
    return std::hash<std::string_view>{}(sv);
  }
  std::size_t operator()(const std::string &s) const noexcept {
    return std::hash<std::string_view>{}(std::string_view(s));
  }
};

struct SchemaNameEq {
  using is_transparent = void;
  bool operator()(std::string_view a, std::string_view b) const noexcept {
    return a == b;
  }
  bool operator()(const std::string &a, std::string_view b) const noexcept {
    return std::string_view(a) == b;
  }
  bool operator()(std::string_view a, const std::string &b) const noexcept {
    return a == std::string_view(b);
  }
  bool operator()(const std::string &a, const std::string &b) const noexcept {
    return a == b;
  }
};

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
 * @brief Primitive kind cached on TypeSchema for fast dispatch in hot loops.
 *
 * PK_NONE  - not a primitive
 * PK_ANY   - typing.Any
 * PK_INT   - int
 * PK_FLOAT - float
 * PK_STR   - str
 * PK_BOOL  - bool
 */
enum PrimitiveKind {
  PK_NONE = 0,
  PK_ANY = 1,
  PK_INT = 2,
  PK_FLOAT = 3,
  PK_STR = 4,
  PK_BOOL = 5
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
 *  - The cached UTF-8 representation of the typeâ€™s repr.
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
  int primitive_kind;
  // Tuple[T, ...] is "variadic": any length, every element of type T.
  // When set, num_args == 1 and args[0] is the element schema.
  int is_variadic_tuple;
  // typing.Literal[v1, v2, ...]: when set, args is unused and the schema
  // matches a value if it equals one of the entries in literal_values.
  // literal_values is a strong reference to a Python tuple of values.
  int is_literal;
  PyObject *literal_values;
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
  Py_ssize_t field_name_len;
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
// Behaviour for keys passed to a model that are not declared in its schema
// (and are not declared aliases either).
enum ExtraPolicy {
  EXTRA_IGNORE = 0, // silently drop (default, backward compatible)
  EXTRA_ALLOW = 1,  // keep on instance via extra_fields and serialize out
  EXTRA_FORBID = 2, // report a validation error naming the unknown keys
};

struct SchemaCache {
  FieldSchema *fields;
  Py_ssize_t num_fields;
  PyObject *config;
  PyObject *dict_serializer;
  PyObject *json_serializer;
  PyObject *instance_annotations;
  PyObject *validators;
  int has_field_before;
  int has_field_after;
  int has_model_before;
  int has_model_after;
  int extra_policy; // one of ExtraPolicy
  Deserializers *deserializers;
#ifdef __cplusplus
  // Map from canonical field name and any alias to the field index in
  // `fields`. Built once at schema compile time so JSON walks can resolve
  // a member name in O(1) with heterogeneous lookup (no per-call
  // std::string allocation).
  std::unordered_map<std::string, Py_ssize_t, SchemaNameHash, SchemaNameEq>
      name_index;
#endif
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
