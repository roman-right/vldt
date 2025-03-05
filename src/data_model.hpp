#pragma once

#include <Python.h>
#include <string>
#include <unordered_map>

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
 * @brief Internal data structure for storing instance attributes.
 *
 * Holds the fields of a DataModel instance in a C++ unordered_map
 * for faster access, avoiding the overhead of Python’s __dict__.
 */
struct InstanceData {
  std::unordered_map<std::string, PyObject *>
      fields;            // Field name to value mapping.
  bool dict_initialized; // Tracks whether __dict__ has been populated.
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
