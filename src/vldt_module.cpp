#include "data_model.hpp"
#include "init_globals.hpp"
#include "validation/validation.hpp"
#include <Python.h>

static PyModuleDef vldtmodule = {
    .m_base =
        {
            .ob_base = {.ob_refcnt = 1, .ob_type = &PyType_Type},
            .m_index = 0,
            .m_copy = 0,
        },
    .m_name = "vldt._vldt",
    .m_doc = "vldt C++ extension module",
    .m_size = -1,
    .m_methods = nullptr,
    .m_slots = nullptr,
    .m_traverse = nullptr,
    .m_clear = nullptr,
    .m_free = nullptr,
};

extern "C" {

PyMODINIT_FUNC PyInit__vldt(void) {
  if (PyType_Ready(&DataModelType) < 0) {
    return nullptr;
  }

  auto m = PyModule_Create(&vldtmodule);
  if (!m) {
    return nullptr;
  }

  if (init_data_model_globals() != 0 || init_validation_globals() != 0) {
    Py_DECREF(m);
    return nullptr;
  }

  Py_INCREF(&DataModelType);
  if (PyModule_AddObject(m, "DataModel", (PyObject *)&DataModelType) < 0) {
    Py_DECREF(&DataModelType);
    Py_DECREF(m);
    return nullptr;
  }
  return m;
}

} // extern "C"
