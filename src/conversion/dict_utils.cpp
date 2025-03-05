#include "dict_utils.hpp"
#include "data_model.hpp"
#include "schema/schema.hpp"
#include <Python.h>
#include <stdlib.h>
#include <string>

extern PyObject *schema_key;
extern PyObject *get_schema_cached(PyObject *cls);

/**
 * @brief Convert a PyObject to its dictionary representation.
 *
 * This function attempts a custom conversion via dict_serializer first.
 * If no custom conversion applies, it dispatches conversion based on the type.
 *
 * @param value The PyObject to convert.
 * @param dict_serializer A dictionary mapping types to custom conversion
 * functions.
 * @return New reference to a PyObject representing the dictionary conversion,
 * or nullptr on error.
 */
PyObject *convert_to_dict(PyObject *value, PyObject *dict_serializer);

/**
 * @brief Checks if a PyObject is of a basic immutable type.
 *
 * A basic immutable type includes int, float, str, bool, None, or bytes.
 *
 * @param value The PyObject to check.
 * @return Non-zero if the object is a basic immutable type; otherwise 0.
 */
static inline int is_basic_immutable(PyObject *value) {
  return PyLong_Check(value) || PyFloat_Check(value) ||
         PyUnicode_Check(value) || (value == Py_None) || PyBool_Check(value) ||
         PyBytes_Check(value);
}

/**
 * @brief Applies a custom dict serializer to a PyObject.
 *
 * If a custom conversion function exists for the type of the value in
 * dict_serializer, it is applied. If conversion is successful and the result is
 * not Py_NotImplemented, a new reference to the converted object is returned.
 *
 * @param value The PyObject to convert.
 * @param dict_serializer A dictionary containing conversion functions.
 * @return New reference to the converted PyObject, or nullptr if no conversion
 * is performed.
 */
static PyObject *apply_dict_serializer(PyObject *value,
                                       PyObject *dict_serializer) {
  if (dict_serializer && PyDict_Check(dict_serializer) &&
      PyDict_Size(dict_serializer) > 0) {
    PyObject *type_obj = (PyObject *)Py_TYPE(value);
    PyObject *conv_func = PyDict_GetItem(dict_serializer, type_obj);
    if (conv_func && PyCallable_Check(conv_func)) {
      PyObject *converted =
          PyObject_CallFunctionObjArgs(conv_func, value, nullptr);
      if (converted && converted != Py_NotImplemented) {
        return converted;
      }
      Py_XDECREF(converted);
    }
  }
  return nullptr;
}

/**
 * @brief Recursively converts a Python list to a new list with converted items.
 *
 * @param list_obj The Python list to convert.
 * @param dict_serializer A dictionary containing conversion functions.
 * @return New list with converted items, or nullptr on error.
 */
static PyObject *convert_list(PyObject *list_obj, PyObject *dict_serializer) {
  Py_ssize_t size = PyList_GET_SIZE(list_obj);
  PyObject *new_list = PyList_New(size);
  if (!new_list) {
    return nullptr;
  }
  for (Py_ssize_t i = 0; i < size; i++) {
    PyObject *item = PyList_GET_ITEM(list_obj, i);
    PyObject *conv_item = convert_to_dict(item, dict_serializer);
    if (!conv_item) {
      Py_DECREF(new_list);
      return nullptr;
    }
    PyList_SET_ITEM(new_list, i, conv_item);
  }
  return new_list;
}

/**
 * @brief Recursively converts a Python dictionary to a new dictionary with
 * converted values.
 *
 * @param dict_obj The Python dictionary to convert.
 * @param dict_serializer A dictionary containing conversion functions.
 * @return New dictionary with converted values, or nullptr on error.
 */
static PyObject *convert_dict(PyObject *dict_obj, PyObject *dict_serializer) {
  PyObject *new_dict = PyDict_New();
  if (!new_dict) {
    return nullptr;
  }
  PyObject *k, *v;
  Py_ssize_t pos = 0;
  while (PyDict_Next(dict_obj, &pos, &k, &v)) {
    PyObject *conv_value = convert_to_dict(v, dict_serializer);
    if (!conv_value) {
      Py_DECREF(new_dict);
      return nullptr;
    }
    if (PyDict_SetItem(new_dict, k, conv_value) != 0) {
      Py_DECREF(conv_value);
      Py_DECREF(new_dict);
      return nullptr;
    }
    Py_DECREF(conv_value);
  }
  return new_dict;
}

/**
 * @brief Recursively converts a Python tuple to a new tuple with converted
 * items.
 *
 * @param tuple_obj The Python tuple to convert.
 * @param dict_serializer A dictionary containing conversion functions.
 * @return New tuple with converted items, or nullptr on error.
 */
static PyObject *convert_tuple(PyObject *tuple_obj, PyObject *dict_serializer) {
  Py_ssize_t size = PyTuple_GET_SIZE(tuple_obj);
  PyObject *new_tuple = PyTuple_New(size);
  if (!new_tuple) {
    return nullptr;
  }
  for (Py_ssize_t i = 0; i < size; i++) {
    PyObject *item = PyTuple_GET_ITEM(tuple_obj, i);
    PyObject *conv_item = convert_to_dict(item, dict_serializer);
    if (!conv_item) {
      Py_DECREF(new_tuple);
      return nullptr;
    }
    PyTuple_SET_ITEM(new_tuple, i, conv_item);
  }
  return new_tuple;
}

/**
 * @brief Recursively converts a Python set to a new set with converted items.
 *
 * @param set_obj The Python set to convert.
 * @param dict_serializer A dictionary containing conversion functions.
 * @return New set with converted items, or nullptr on error.
 */
static PyObject *convert_set(PyObject *set_obj, PyObject *dict_serializer) {
  PyObject *new_set = PySet_New(nullptr);
  if (!new_set) {
    return nullptr;
  }
  PyObject *iterator = PyObject_GetIter(set_obj);
  if (!iterator) {
    Py_DECREF(new_set);
    return nullptr;
  }
  PyObject *item;
  while ((item = PyIter_Next(iterator))) {
    PyObject *conv_item = convert_to_dict(item, dict_serializer);
    Py_DECREF(item);
    if (!conv_item) {
      Py_DECREF(new_set);
      Py_DECREF(iterator);
      return nullptr;
    }
    if (PySet_Add(new_set, conv_item) != 0) {
      Py_DECREF(conv_item);
      Py_DECREF(new_set);
      Py_DECREF(iterator);
      return nullptr;
    }
    Py_DECREF(conv_item);
  }
  Py_DECREF(iterator);
  if (PyErr_Occurred()) {
    Py_DECREF(new_set);
    return nullptr;
  }
  return new_set;
}

/**
 * @brief Converts a DataModel instance to a dictionary.
 *
 * Retrieves the cached schema for the model and iterates over its fields to
 * build a dictionary representation using the schema's dict_serializer for
 * nested conversions.
 *
 * @param value The DataModel instance to convert.
 * @return New dictionary representing the DataModel, or nullptr on error.
 */
static PyObject *convert_datamodel(PyObject *value) {
  PyTypeObject *type_ptr = Py_TYPE(value);
  PyObject *capsule = get_schema_cached((PyObject *)type_ptr);
  if (!capsule) {
    return nullptr;
  }
  SchemaCache *schema =
      (SchemaCache *)PyCapsule_GetPointer(capsule, "vldt.SchemaCache");
  Py_DECREF(capsule);
  if (!schema) {
    return nullptr;
  }
  PyObject *dict_serializer = schema->dict_serializer;
  PyObject *result_dict = PyDict_New();
  if (!result_dict) {
    return nullptr;
  }
  DataModelObject *bm = (DataModelObject *)value;
  for (Py_ssize_t i = 0; i < schema->num_fields; i++) {
    FieldSchema *fs = &schema->fields[i];
    std::string field_key(fs->field_name_c);
    auto it = bm->instance_data->fields.find(field_key);
    if (it == bm->instance_data->fields.end()) {
      continue;
    }
    PyObject *field_value = it->second;
    PyObject *conv_value = convert_to_dict(field_value, dict_serializer);
    if (!conv_value) {
      Py_DECREF(result_dict);
      return nullptr;
    }
    if (PyDict_SetItem(result_dict, fs->field_name, conv_value) != 0) {
      Py_DECREF(conv_value);
      Py_DECREF(result_dict);
      return nullptr;
    }
    Py_DECREF(conv_value);
  }
  return result_dict;
}

/**
 * @brief Converts a PyObject to its dictionary representation.
 *
 * Attempts to convert using a custom dict_serializer first. If not applicable,
 * conversion is dispatched based on the type of the PyObject.
 *
 * @param value The PyObject to convert.
 * @param dict_serializer A dictionary mapping types to custom conversion
 * functions.
 * @return New reference to the converted PyObject, or nullptr on error.
 */
PyObject *convert_to_dict(PyObject *value, PyObject *dict_serializer) {
  if (dict_serializer) {
    if (PyObject *encoded = apply_dict_serializer(value, dict_serializer)) {
      return encoded;
    }
  }
  if (is_basic_immutable(value)) {
    Py_INCREF(value);
    return value;
  }
  if (PyObject_IsInstance(value, (PyObject *)&DataModelType) == 1) {
    return convert_datamodel(value);
  }
  if (PyList_Check(value)) {
    return convert_list(value, dict_serializer);
  }
  if (PyDict_Check(value)) {
    return convert_dict(value, dict_serializer);
  }
  if (PyTuple_Check(value)) {
    return convert_tuple(value, dict_serializer);
  }
  if (PySet_Check(value)) {
    return convert_set(value, dict_serializer);
  }
  Py_INCREF(value);
  return value;
}

/**
 * @brief Create a DataModel instance from a dictionary.
 *
 * Parses the input arguments to extract a dictionary and constructs a new
 * instance by calling the model class.
 *
 * @param cls The model class.
 * @param args Tuple containing the input dictionary.
 * @return A new DataModel instance, or nullptr on failure.
 */
PyObject *dict_utils_from_dict(PyObject *cls, PyObject *args) {
  PyObject *input_dict = nullptr;
  if (!PyArg_ParseTuple(args, "O!", &PyDict_Type, &input_dict)) {
    return nullptr;
  }
  PyObject *empty_tuple = PyTuple_New(0);
  if (!empty_tuple) {
    return nullptr;
  }
  PyObject *instance = PyObject_Call(cls, empty_tuple, input_dict);
  Py_DECREF(empty_tuple);
  return instance;
}

/**
 * @brief Convert a DataModel instance to a dictionary.
 *
 * Delegates the conversion to convert_datamodel, which constructs a dictionary
 * representation of the DataModel instance.
 *
 * @param self The DataModel instance.
 * @param Py_UNUSED(ignored) Unused parameter.
 * @return New dictionary representing the DataModel instance.
 */
PyObject *dict_utils_to_dict(PyObject *self, PyObject *Py_UNUSED(ignored)) {
  return convert_datamodel(self);
}
