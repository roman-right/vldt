#pragma once

#include <Python.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef __cplusplus
#include <rapidjson/document.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize globals used by DataModel.
 *
 * Creates interned strings, loads the Field type from the module,
 * and performs any necessary global initialization.
 *
 * @return 0 on success, -1 on failure.
 */
int init_data_model_globals(void);

/**
 * @brief DataModel.__init__ implementation.
 *
 * Validates and sets attributes from keyword arguments.
 *
 * @param self The model instance.
 * @param args Positional arguments (should be empty).
 * @param kwds Keyword arguments for field values.
 * @return 0 on success, -1 on failure.
 */
int DataModel_init(PyObject *self, PyObject *args, PyObject *kwds);

/**
 * @brief DataModel.__setattro__ implementation.
 *
 * Enforces type validation when setting attributes.
 *
 * @param self The model instance.
 * @param name The attribute name.
 * @param value The value to set.
 * @return 0 on success, -1 on failure.
 */
int DataModel_setattro(PyObject *self, PyObject *name, PyObject *value);

/**
 * @brief DataModel.__getattro__ implementation.
 *
 * Retrieves attributes from the internal C++ data structure.
 *
 * @param self The model instance.
 * @param name The attribute name.
 * @return The attribute value, or nullptr if not found.
 */
PyObject *DataModel_getattro(PyObject *self, PyObject *name);

/**
 * @brief DataModel.__new__ implementation.
 *
 * Allocates memory for the DataModel instance and initializes
 * the internal C++ data structure.
 *
 * @param type The Python type.
 * @param args Positional arguments.
 * @param kwds Keyword arguments.
 * @return A new instance of DataModel, or nullptr on failure.
 */
PyObject *DataModel_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

/**
 * @brief DataModel.__dealloc__ implementation.
 *
 * Cleans up the internal C++ data structure and releases
 * references to Python objects.
 *
 * @param self The model instance.
 */
void DataModel_dealloc(PyObject *self);

/**
 * The DataModel type object.
 */
extern PyTypeObject DataModelType;

/**
 * An interned key used for caching a compiled SchemaCache on a model class.
 */
extern PyObject *schema_key;

/**
 * @brief Initialize a DataModel instance from a native JSON object.
 *
 * The JSON root must be an object. This function performs conversion,
 * validation (using a centralized ErrorCollector), and stores the resulting
 * values in the internal data structure.
 *
 * @param self The model instance.
 * @param native The rapidjson DOM element representing the JSON object.
 * @return 0 on success, -1 on failure.
 */
int DataModel_init_from_native(PyObject *self, const rapidjson::Value &native);

#ifdef __cplusplus
} // end extern "C"
#endif

#ifdef __cplusplus
/**
 * @brief Transparent hasher for unordered_map<string, V> heterogeneous lookup.
 *
 * Allows find()/operator[] to be called with const char*, std::string_view,
 * or std::string without constructing a temporary std::string. Reused by the
 * extra_fields fallback below.
 */
struct VldtStringHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view sv) const noexcept {
    return std::hash<std::string_view>{}(sv);
  }
  std::size_t operator()(const std::string &s) const noexcept {
    return std::hash<std::string_view>{}(std::string_view(s));
  }
  std::size_t operator()(const char *s) const noexcept {
    return std::hash<std::string_view>{}(std::string_view(s));
  }
};

struct VldtStringEq {
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
  bool operator()(const char *a, const std::string &b) const noexcept {
    return std::string_view(a) == std::string_view(b);
  }
  bool operator()(const std::string &a, const char *b) const noexcept {
    return std::string_view(a) == std::string_view(b);
  }
};

using ExtraFieldsMap =
    std::unordered_map<std::string, PyObject *, VldtStringHash, VldtStringEq>;

/**
 * @brief Internal data structure for storing instance attributes.
 *
 * Schema-declared fields live in `fields`, a flat vector indexed by the
 * field's position in the model's SchemaCache::fields array. Writes are
 * O(1) with no hashing and no per-field heap allocation, and reads use
 * SchemaCache::name_index to map a name to a vector index in O(1).
 *
 * Attributes that are not declared on the schema (set via
 * `obj.something = ...`) live in the lazily-allocated `extra_fields`
 * unordered_map, so the rare path keeps its previous semantics without
 * imposing any cost on the schema-driven hot path.
 *
 * cached_schema is a borrowed pointer to the model class's compiled
 * SchemaCache. It is set once during DataModel_init so hot paths such as
 * to_dict and to_json can iterate fields in schema order without repeating
 * the tp_dict capsule lookup on every call.
 */
struct InstanceData {
  std::vector<PyObject *> fields;
  std::unique_ptr<ExtraFieldsMap> extra_fields;
  bool dict_initialized;
  void *cached_schema;
};

/**
 * @brief DataModel object structure.
 *
 * Extends PyObject to include a pointer to the internal C++ data structure.
 */
typedef struct {
  PyObject_HEAD InstanceData
      *instance_data; // Pointer to the native instance data.
} DataModelObject;
#endif // __cplusplus
