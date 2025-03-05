#include "validation_validators.hpp"
#include <Python.h>

/**
 * @brief Returns a new reference to a callable validator.
 *
 * If the provided validator is callable, it returns it (with an extra
 * reference); otherwise, if it has a "__func__" attribute that is callable, it
 * returns that. Returns nullptr if neither is callable.
 *
 * @param validator The original validator object.
 * @return New reference to a callable validator, or nullptr.
 */
static PyObject *get_callable_validator(PyObject *validator) {
  if (PyCallable_Check(validator)) {
    Py_INCREF(validator);
    return validator;
  }
  if (PyObject_HasAttrString(validator, "__func__")) {
    PyObject *func = PyObject_GetAttrString(validator, "__func__");
    if (func && PyCallable_Check(func)) {
      return func;
    }
    Py_XDECREF(func);
  }
  return nullptr;
}

/**
 * @brief Runs model validators.
 *
 * Iterates over validator_list and calls each callable validator on target,
 * optionally including cls as argument.
 *
 * @param cls The model class.
 * @param validator_list List of validator objects.
 * @param target The target PyObject to validate.
 * @param call_with_cls Flag indicating if cls should be passed.
 * @return 0 on success, -1 on error.
 */
int run_model_validators(PyObject *cls, PyObject *validator_list,
                         PyObject *target, int call_with_cls) {
  Py_ssize_t len = PyList_Size(validator_list);
  if (len == 0) {
    return 0;
  }
  for (Py_ssize_t i = 0; i < len; i++) {
    PyObject *validator = PyList_GetItem(validator_list, i);
    PyObject *callable_validator = get_callable_validator(validator);
    if (!callable_validator) {
      continue;
    }
    PyObject *result =
        call_with_cls
            ? PyObject_CallFunctionObjArgs(callable_validator, cls, target,
                                           nullptr)
            : PyObject_CallFunctionObjArgs(callable_validator, target, nullptr);
    Py_DECREF(callable_validator);
    if (!result) {
      return -1;
    }
    if (result && PyDict_Check(result) && call_with_cls) {
      if (PyDict_Update(target, result) != 0) {
        Py_DECREF(result);
        return -1;
      }
    }
    Py_DECREF(result);
  }
  return 0;
}

/**
 * @brief Runs field before validators.
 *
 * Applies 'field_before' validators from the schema on the provided keyword
 * arguments dictionary.
 *
 * @param schema SchemaCache containing validators.
 * @param cls The model class.
 * @param pKwds Pointer to the keyword arguments dictionary.
 * @return 0 on success, -1 on error.
 */
int run_field_before_validators(SchemaCache *schema, PyObject *cls,
                                PyObject **pKwds) {
  if (!schema->has_field_before) {
    return 0;
  }
  PyObject *field_before =
      PyDict_GetItemString(schema->validators, "field_before");
  if (field_before && PyDict_Check(field_before) &&
      PyDict_Size(field_before) == 0) {
    return 0;
  }
  if (field_before && PyDict_Check(field_before)) {
    PyObject *key;
    PyObject *val;
    Py_ssize_t pos = 0;
    while (PyDict_Next(field_before, &pos, &key, &val)) {
      if (PyDict_Contains(*pKwds, key)) {
        PyObject *current_value = PyDict_GetItem(*pKwds, key);
        Py_INCREF(current_value);
        if (val && PyList_Check(val)) {
          Py_ssize_t len_val = PyList_Size(val);
          for (Py_ssize_t i = 0; i < len_val; i++) {
            PyObject *validator = PyList_GetItem(val, i);
            PyObject *callable_validator = get_callable_validator(validator);
            if (callable_validator && PyCallable_Check(callable_validator)) {
              PyObject *new_value = PyObject_CallFunctionObjArgs(
                  callable_validator, cls, current_value, nullptr);
              Py_DECREF(callable_validator);
              if (!new_value) {
                Py_DECREF(current_value);
                return -1;
              }
              Py_DECREF(current_value);
              current_value = new_value;
            } else if (callable_validator) {
              Py_DECREF(callable_validator);
            }
          }
        }
        if (PyDict_SetItem(*pKwds, key, current_value) < 0) {
          Py_DECREF(current_value);
          return -1;
        }
        Py_DECREF(current_value);
      }
    }
  }
  return 0;
}

/**
 * @brief Runs model before validators.
 *
 * Applies 'model_before' validators from the schema on the provided keyword
 * arguments dictionary.
 *
 * @param schema SchemaCache containing validators.
 * @param cls The model class.
 * @param pKwds Pointer to the keyword arguments dictionary.
 * @return 0 on success, -1 on error.
 */
int run_model_before_validators(SchemaCache *schema, PyObject *cls,
                                PyObject **pKwds) {
  if (!schema->has_model_before) {
    return 0;
  }
  PyObject *model_before =
      PyDict_GetItemString(schema->validators, "model_before");
  if (model_before && PyList_Check(model_before)) {
    if (PyList_Size(model_before) == 0) {
      return 0;
    }
    return run_model_validators(cls, model_before, *pKwds, 1);
  }
  return 0;
}

/**
 * @brief Runs field after validators.
 *
 * Applies 'field_after' validators from the schema on the attributes of the
 * instance.
 *
 * @param schema SchemaCache containing validators.
 * @param cls The model class.
 * @param self The instance to validate.
 * @return 0 on success, -1 on error.
 */
int run_field_after_validators(SchemaCache *schema, PyObject *cls,
                               PyObject *self) {
  if (!schema->has_field_after) {
    return 0;
  }
  PyObject *field_after =
      PyDict_GetItemString(schema->validators, "field_after");
  if (field_after && PyDict_Check(field_after)) {
    if (PyDict_Size(field_after) == 0) {
      return 0;
    }
    PyObject *key;
    PyObject *val;
    Py_ssize_t pos = 0;
    while (PyDict_Next(field_after, &pos, &key, &val)) {
      if (PyObject_HasAttr(self, key)) {
        PyObject *current_value = PyObject_GetAttr(self, key);
        if (current_value && PyList_Check(val)) {
          Py_ssize_t len_val = PyList_Size(val);
          for (Py_ssize_t i = 0; i < len_val; i++) {
            PyObject *validator = PyList_GetItem(val, i);
            PyObject *callable_validator = get_callable_validator(validator);
            if (callable_validator && PyCallable_Check(callable_validator)) {
              PyObject *new_value = PyObject_CallFunctionObjArgs(
                  callable_validator, cls, current_value, nullptr);
              Py_DECREF(callable_validator);
              if (!new_value) {
                Py_DECREF(current_value);
                return -1;
              }
              Py_DECREF(current_value);
              current_value = new_value;
            } else if (callable_validator) {
              Py_DECREF(callable_validator);
            }
          }
          if (PyObject_SetAttr(self, key, current_value) < 0) {
            Py_DECREF(current_value);
            return -1;
          }
          Py_DECREF(current_value);
        }
      }
    }
  }
  return 0;
}

/**
 * @brief Runs model after validators.
 *
 * Applies 'model_after' validators from the schema on the instance.
 *
 * @param schema SchemaCache containing validators.
 * @param cls The model class.
 * @param self The instance to validate.
 * @return 0 on success, -1 on error.
 */
int run_model_after_validators(SchemaCache *schema, PyObject *cls,
                               PyObject *self) {
  if (!schema->has_model_after) {
    return 0;
  }
  PyObject *model_after =
      PyDict_GetItemString(schema->validators, "model_after");
  if (model_after && PyList_Check(model_after)) {
    if (PyList_Size(model_after) == 0) {
      return 0;
    }
    Py_ssize_t len = PyList_Size(model_after);
    for (Py_ssize_t i = 0; i < len; i++) {
      PyObject *validator = PyList_GetItem(model_after, i);
      PyObject *callable_validator = get_callable_validator(validator);
      if (!callable_validator) {
        continue;
      }
      int argcount = 0;
      PyObject *code = PyObject_GetAttrString(validator, "__code__");
      if (code) {
        PyObject *argcount_obj = PyObject_GetAttrString(code, "co_argcount");
        if (argcount_obj && PyLong_Check(argcount_obj)) {
          argcount = static_cast<int>(PyLong_AsLong(argcount_obj));
        }
        Py_XDECREF(argcount_obj);
        Py_DECREF(code);
      }
      if (argcount == 1) {
        if (!PyObject_CallFunctionObjArgs(callable_validator, self, nullptr)) {
          Py_DECREF(callable_validator);
          return -1;
        }
      } else {
        if (!PyObject_CallFunctionObjArgs(callable_validator, cls, self,
                                          nullptr)) {
          Py_DECREF(callable_validator);
          return -1;
        }
      }
      Py_DECREF(callable_validator);
    }
  }
  return 0;
}
