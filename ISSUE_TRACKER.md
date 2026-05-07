# vldt Issue Tracker

This file tracks the current work on repository issues and fixes.

| Issue | Description | Status | Notes |
|---|---|---|---|
| #19 | `__copy__` should mirror `__deepcopy__` using `copy.copy` instead of `copy.deepcopy` | Done | Added `__copy__` and regression tests |
| #20 | `from_dict` / `from_json` key validation must ensure keys are `str` / `PyUnicode` | Done | Added `PyUnicode_Check` validation for mapping keys and regression tests |
| #21 | Async/sync validator detection unification | Pending | Resolve `__func__` and async flag at decoration time |
| #29 | Serializer MRO fallback | Pending | Walk `tp_mro` on serializer miss |
| #30 | Deserializer MRO fallback | Pending | Walk `tp_mro` on deserializer miss |
| #31 | Recompiling `TypeSchema` on `__setattr__` | Pending | Use field compiled schema from `SchemaCache` |
| #32 | `PyObject_RichCompareBool` for type identity | Pending | Use pointer comparison for type singletons |
| #33 | `validate_plain` fast path | Pending | Add `Py_TYPE(value) == (PyTypeObject*)ts->expected_type` before `isinstance` |
| #34 | Unused `is_dict_initialized` field | Pending | Remove dead field from `InstanceData` |
| #35 | Error path truncation | Pending | Replace fixed 256-char buffer with `std::string` |
| #36 | Container rebuilds even when unchanged | Pending | Track conversion-change flag and return original if unchanged |
| #37 | `SchemaCache` unused cached members | Pending | Remove dead fields from schema cache structures |
| #38 | Validator dict lookup at runtime | Pending | Cache borrowed list pointers in `SchemaCache` at compile time |
| #39 | `__setattro__` deletion handling | Pending | Support `value == nullptr` to clear slot or raise properly |
| #40 | `__repr__` | Pending | Generate representation from schema fields |
| #42 | `field_validator` accept explicit field names | Pending | Accept `*fields` and store list |
| #43 | `Field(alias=...)` type validation | Pending | Add `isinstance(alias, str)` check in `Field.__init__` |
| #44 | `GLOBAL_DESERIALIZER` opt-out | Pending | Add `Config(use_default_deserializers=False)` flag |
| #45 | `get_callable_validator` per-iteration | Pending | Resolve once at decoration time |
| #46 | Alias collision detection | Pending | Check `name_index` duplicates at class definition |
| #47 | Test coverage gaps (threading, GC, pickle) | Pending | Add tests after feature work |
| #64 | NamedTuple coercion | Pending | Detect `_fields` + tuple subclass and call `T(*value)` |
| #48 | `models.pyi` complete | Pending | Add stubs for I/O helpers |
| #49 | README typos | Pending | Fix text errors |
| #50 | `install.sh` simplification | Pending | Replace with `pip install -e .` |
| #52 | `__version__` export | Pending | Use `importlib.metadata.version` |
| #53 | `safe_type_name` duplicated | Pending | Hoist to shared header |
| #54 | Unused includes in `json_utils.cpp` | Pending | Delete `<iostream>`, `<chrono>` |
| #55 | Unused `<atomic>` | Pending | Delete unused include |
| #56 | Duplicated extern declarations | Pending | Move to shared header |
| #57 | `FieldType` detection fragile | Pending | Use `PyObject_IsInstance` consistently |
| #58 | `MANIFEST.in` redundancies | Pending | Remove redundant lines |
| #59 | `pyproject.toml` / `setup.py` duplication | Pending | Move metadata into `pyproject.toml` |
| #60 | Performance section invalid | Pending | Verify README refresh |
| #61 | No `py.typed` | Pending | Add `vldt/py.typed` |
| #62 | Undeclared matplotlib dep | Pending | Add optional dev dependency |
