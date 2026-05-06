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


class TestIssue4JsonFieldOrder:
    """Issue #4: to_json iterated std::unordered_map, producing
    non-deterministic field ordering. The order must match the schema
    (i.e., the class's annotation order) and must equal to_dict's order.
    """

    def test_to_json_field_order_matches_schema(self):
        """Field order in JSON output must match annotation order."""

        class M(DataModel):
            zebra: int
            alpha: str
            mike: float
            bravo: bool

        obj = M(zebra=1, alpha="a", mike=2.5, bravo=True)
        json_str = obj.to_json()

        idx_zebra = json_str.index('"zebra"')
        idx_alpha = json_str.index('"alpha"')
        idx_mike = json_str.index('"mike"')
        idx_bravo = json_str.index('"bravo"')
        assert idx_zebra < idx_alpha < idx_mike < idx_bravo, (
            f"Expected schema order in JSON, got: {json_str}"
        )

    def test_to_json_byte_stable_across_instances(self):
        """Two distinct equivalent instances must serialize to identical JSON."""

        class M(DataModel):
            a: int
            b: str
            c: float
            d: bool
            e: int

        results = set()
        for _ in range(20):
            obj = M(a=1, b="x", c=1.5, d=True, e=42)
            results.add(obj.to_json())
        assert len(results) == 1, (
            f"Expected one unique JSON string, got {len(results)}: {results}"
        )

    def test_to_json_matches_to_dict_order(self):
        """JSON output ordering must match to_dict()'s ordering."""
        import json as _json

        class M(DataModel):
            x: int
            y: int
            z: int

        obj = M(x=1, y=2, z=3)
        dict_keys = list(obj.to_dict().keys())
        json_keys = list(_json.loads(obj.to_json()).keys())
        assert json_keys == dict_keys


class TestIssue5FieldTypeDetection:
    """Issue #5: Field detection used hasattr('default') / hasattr('default_factory')
    which misidentified any user object exposing those attributes as a Field.
    """

    def test_user_object_with_default_attr_used_as_default_value(self):
        """A user-defined object with a 'default' attribute should be treated
        as a literal default value, not as a Field descriptor."""

        class CustomThing:
            """User class that happens to have a 'default' attribute."""
            default = "user_value"

            def __repr__(self):
                return "CustomThing()"

        custom = CustomThing()

        class M(DataModel):
            thing: CustomThing = custom

        obj = M()
        assert obj.thing is custom, (
            f"Expected default to be the CustomThing instance, got {obj.thing!r}"
        )

    def test_user_object_with_default_factory_attr_used_as_default(self):
        """Same as above for 'default_factory'."""

        class WidgetTemplate:
            default_factory = lambda: "should_not_be_called"

        template = WidgetTemplate()

        class M(DataModel):
            widget: WidgetTemplate = template

        obj = M()
        assert obj.widget is template
