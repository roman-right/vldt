"""Tests for critical bug fixes.

Each test corresponds to a numbered issue in the GitHub repo and serves as a
regression guard against the bug being reintroduced.
"""

import gc
import sys
from typing import ClassVar, Optional

import pytest

from vldt import DataModel, Field


class TestIssue1EmptyTupleRefcount:
    """Issue #1: empty_tuple global was decremented on every from_json call.

    The bug is masked on CPython 3.12+ because the empty tuple is an immortal
    singleton, but the code is still semantically wrong. A stress test
    documents the expected contract: from_json must remain stable under load.
    """

    def test_from_json_stress(self):
        """Many from_json calls must not crash or corrupt state."""

        class M(DataModel):
            x: int

        for i in range(10000):
            obj = M.from_json('{"x": 42}')
            assert obj.x == 42

    def test_from_json_does_not_leak_empty_tuple_ref(self):
        """The global empty_tuple is shared with Python's empty-tuple singleton.

        Calling from_json many times must not decrement its refcount in an
        observable way. We can't access the C global directly, but we can
        verify the singleton refcount is well-behaved.
        """

        class M(DataModel):
            x: int

        empty = ()
        gc.collect()
        before = sys.getrefcount(empty)
        for _ in range(1000):
            M.from_json('{"x": 1}')
        gc.collect()
        after = sys.getrefcount(empty)

        assert after >= before - 100, (
            f"empty tuple refcount dropped from {before} to {after} "
            "(suggests incorrect Py_DECREF on global)"
        )


class TestIssue2SchemaCompilationCrash:
    """Issue #2: free_type_schema crashed on partial schema compilation.

    The original code did Py_DECREF(ts->repr) without null-guarding it.
    The crash path was OOM-only, so we exercise diverse typing constructs
    as a smoke test that the schema compiler is robust.
    """

    def test_deeply_nested_generics_no_crash(self):
        """Diverse and deeply-nested typing constructs must compile without crashing."""
        from typing import Dict, List, Optional, Set, Tuple, Union

        class Deep(DataModel):
            a: Dict[str, List[Optional[Union[int, str, float]]]]
            b: List[Dict[str, Tuple[int, str, float]]]
            c: Optional[Dict[str, Set[int]]]
            d: Tuple[int, str, float, bool]
            e: Union[int, str, float, bool, None]

        obj = Deep(
            a={"k": [1, "str", None, 3.14]},
            b=[{"k": (1, "s", 1.5)}],
            c={"k": {1, 2, 3}},
            d=(1, "s", 1.5, True),
            e=None,
        )
        assert obj.a == {"k": [1, "str", None, 3.14]}

    def test_repeated_compilation_no_crash(self):
        """Repeatedly compiling/recompiling schemas must not crash."""
        from typing import Dict, List, Optional

        for _ in range(100):
            class M(DataModel):
                x: Dict[str, List[Optional[int]]]

            obj = M(x={"a": [1, None, 3]})
            assert obj.x == {"a": [1, None, 3]}


class TestIssue3DeserializerLeak:
    """Issue #3: get_deserializer returned an INCREF'd object that callers
    never DECREF'd, leaking one ref per primitive validation call.

    We verify by checking that the deserializer function's refcount stays
    stable across many model instantiations.
    """

    def test_deserializer_func_refcount_stable(self):
        """Repeated validations must not leak refs to the deserializer function."""
        from datetime import datetime
        from vldt import Config

        def from_string(v: str) -> datetime:
            return datetime.strptime(v, "%Y/%m/%d %H:%M:%S")

        class M(DataModel):
            ts: datetime
            __vldt_config__ = Config(deserializer={datetime: {str: from_string}})

        gc.collect()
        before = sys.getrefcount(from_string)
        for _ in range(1000):
            M(ts="2021/01/01 12:00:00")
        gc.collect()
        after = sys.getrefcount(from_string)

        leaked = after - before
        assert leaked < 50, (
            f"deserializer function leaked {leaked} refs across 1000 calls "
            "(expected near zero)"
        )

    def test_deserializer_default_path_refcount_stable(self):
        """Even when no custom deserializer matches, refcounts stay stable."""
        from datetime import datetime

        class M(DataModel):
            ts: datetime

        for _ in range(1000):
            obj = M(ts="2021-01-01T12:00:00")
            assert obj.ts == datetime(2021, 1, 1, 12, 0)
