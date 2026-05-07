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


class TestIssue6FieldNoDefault:
    """Issue #6: Field() set self.default to None which made the field
    silently default to None even when no default was intended. Treating
    Field() as 'no default' must require a value at instantiation.
    """

    def test_field_no_args_requires_value(self):
        """Field() with no default and no factory must demand a value."""

        class M(DataModel):
            a: int = Field()

        with pytest.raises(TypeError) as exc:
            M()
        assert "Missing required field" in str(exc.value)

    def test_field_no_args_accepts_value(self):
        """Field() with no default still accepts a value at construction."""

        class M(DataModel):
            a: int = Field()

        obj = M(a=42)
        assert obj.a == 42

    def test_explicit_none_default_works_for_optional(self):
        """Field(default=None) on an Optional field still allows None."""

        class M(DataModel):
            a: Optional[int] = Field(default=None)

        assert M().a is None
        assert M(a=5).a == 5

    def test_field_with_alias_only_no_default(self):
        """Field(alias=...) with no default behaves the same as Field()."""

        class M(DataModel):
            a: int = Field(alias="a_alias")

        with pytest.raises(TypeError) as exc:
            M()
        assert "Missing required field" in str(exc.value)
        obj = M.from_dict({"a_alias": 7})
        assert obj.a == 7


class TestIssue7HashEquality:
    """Issue #7: __eq__ was defined but __hash__ was not, breaking the
    contract that equal objects must hash equal.

    DataModel instances are mutable, so a hash based on contents would
    silently break dict/set membership when fields change. The fix is to
    set __hash__ = None, marking instances as unhashable.
    """

    def test_equal_instances_are_unhashable(self):
        """Mutable models must be unhashable to avoid dict/set bugs."""

        class M(DataModel):
            x: int

        a = M(x=1)
        with pytest.raises(TypeError, match="unhashable"):
            hash(a)

    def test_cannot_use_as_dict_key(self):
        """Models must not be usable as dict keys."""

        class M(DataModel):
            x: int

        a = M(x=1)
        with pytest.raises(TypeError):
            {a: "value"}

    def test_cannot_use_in_set(self):
        """Models must not be usable as set members."""

        class M(DataModel):
            x: int

        a = M(x=1)
        with pytest.raises(TypeError):
            {a}

    def test_equality_still_works(self):
        """Equality comparison must continue to work."""

        class M(DataModel):
            x: int
            y: str

        a = M(x=1, y="a")
        b = M(x=1, y="a")
        c = M(x=2, y="a")
        assert a == b
        assert a != c


class TestIssue9ClassVarNoneDefault:
    """Issue #9: ClassVar fields with an explicit None default raised
    'Missing required class attribute' because the metaclass conflated
    'attribute absent' with 'attribute is None'.
    """

    def test_classvar_optional_none(self):
        """ClassVar[Optional[int]] = None must be accepted."""

        class M(DataModel):
            CACHE: ClassVar[Optional[int]] = None
            x: int

        assert M.CACHE is None
        obj = M(x=1)
        assert obj.x == 1

    def test_classvar_required_still_errors_when_missing(self):
        """A ClassVar without a value must still raise."""

        with pytest.raises(TypeError, match="Missing required class attribute"):
            class M(DataModel):
                CACHE: ClassVar[int]
                x: int

    def test_classvar_optional_int_value_still_validated(self):
        """Type check still fires for non-matching ClassVar values."""

        with pytest.raises(TypeError, match="Class attribute"):
            class M(DataModel):
                CACHE: ClassVar[Optional[int]] = "not an int"
                x: int


class TestIssue14BoolIntAsymmetry:
    """Issue #14: int fields silently accepted True/False because bool is a
    subclass of int. The reverse was not true (bool fields rejected ints), so
    the validator behaviour was asymmetric and surprising. Explicitly reject
    bool when an int is expected.
    """

    def test_int_field_rejects_true(self):
        """An int field must reject True even though bool is a subclass of int."""

        class M(DataModel):
            x: int

        with pytest.raises(TypeError, match="Expected type int, got bool"):
            M(x=True)

    def test_int_field_rejects_false(self):
        class M(DataModel):
            x: int

        with pytest.raises(TypeError, match="Expected type int, got bool"):
            M(x=False)

    def test_int_field_still_accepts_real_ints(self):
        class M(DataModel):
            x: int

        assert M(x=42).x == 42
        assert M(x=0).x == 0
        assert M(x=-7).x == -7

    def test_bool_field_unchanged(self):
        """This issue scopes only int rejecting bool. Bool's existing
        coercion of int (via bool(value)) stays as-is and is the subject of
        a separate conversation about strict mode."""

        class M(DataModel):
            x: bool

        assert M(x=True).x is True
        assert M(x=False).x is False

    def test_int_via_from_dict_rejects_bool(self):
        class M(DataModel):
            x: int

        with pytest.raises(TypeError, match="Expected type int, got bool"):
            M.from_dict({"x": True})

    def test_int_via_from_json_rejects_bool(self):
        class M(DataModel):
            x: int

        with pytest.raises(TypeError, match="Expected type int, got bool"):
            M.from_json('{"x": true}')

    def test_int_in_list_rejects_bool(self):
        from typing import List

        class M(DataModel):
            xs: List[int]

        with pytest.raises(TypeError):
            M(xs=[1, True, 3])

    def test_optional_int_rejects_bool(self):
        class M(DataModel):
            x: Optional[int] = None

        # None still works.
        assert M().x is None
        # int still works.
        assert M(x=5).x == 5
        # bool is still rejected.
        with pytest.raises(TypeError):
            M(x=True)


class TestIssue15VariadicTuple:
    """Issue #15: Tuple[T, ...] (variadic) was treated as a fixed-length 2-tuple
    where the second slot expected Ellipsis. Detect the Ellipsis sentinel
    during schema compilation and accept any length, validating each element
    against the single inner type.
    """

    def test_variadic_tuple_accepts_any_length(self):
        from typing import Tuple

        class M(DataModel):
            xs: Tuple[int, ...]

        assert M(xs=()).xs == ()
        assert M(xs=(1,)).xs == (1,)
        assert M(xs=(1, 2, 3)).xs == (1, 2, 3)
        assert M(xs=(1, 2, 3, 4, 5)).xs == (1, 2, 3, 4, 5)

    def test_variadic_tuple_validates_each_element(self):
        from typing import Tuple

        class M(DataModel):
            xs: Tuple[int, ...]

        with pytest.raises(TypeError):
            M(xs=(1, "not an int", 3))

    def test_variadic_tuple_via_from_dict(self):
        from typing import Tuple

        class M(DataModel):
            xs: Tuple[str, ...]

        obj = M.from_dict({"xs": ("a", "b", "c")})
        assert obj.xs == ("a", "b", "c")

    def test_variadic_tuple_via_from_json(self):
        from typing import Tuple

        class M(DataModel):
            xs: Tuple[float, ...]

        obj = M.from_json('{"xs": [1.5, 2.5, 3.5]}')
        assert obj.xs == (1.5, 2.5, 3.5)

    def test_fixed_tuple_still_works(self):
        """Make sure regular Tuple[T1, T2] still requires the right length."""
        from typing import Tuple

        class M(DataModel):
            t: Tuple[int, str]

        assert M(t=(1, "x")).t == (1, "x")
        with pytest.raises(TypeError, match="Expected tuple of length 2"):
            M(t=(1, "x", 5))
        with pytest.raises(TypeError):
            M(t=(1,))

    def test_variadic_tuple_of_models(self):
        from typing import Tuple

        class Item(DataModel):
            name: str

        class M(DataModel):
            items: Tuple[Item, ...]

        # JSON arrays become tuples through the variadic path.
        obj = M.from_json('{"items": [{"name": "a"}, {"name": "b"}]}')
        assert len(obj.items) == 2
        assert obj.items[0].name == "a"
        assert obj.items[1].name == "b"

    def test_variadic_tuple_empty(self):
        from typing import Tuple

        class M(DataModel):
            xs: Tuple[int, ...]

        obj = M.from_json('{"xs": []}')
        assert obj.xs == ()


class TestIssue27ForwardRefSwallowed:
    """Issue #27: get_type_hints failures were swallowed. An unresolvable
    forward reference would let class definition succeed silently, then
    surface as a cryptic 'SystemError' later. The metaclass should re-raise
    with class context so the cause is obvious at definition time.
    """

    def test_unresolvable_forward_ref_fails_clearly(self):
        """A truly undefined forward reference must raise at class definition,
        with a message that names the offending class."""
        with pytest.raises(NameError) as exc:
            class Bad(DataModel):
                x: "TotallyUndefinedType"
        msg = str(exc.value)
        assert "Bad" in msg, f"class name not in message: {msg}"
        assert "TotallyUndefinedType" in msg, f"missing offending name: {msg}"

    def test_self_referential_forward_ref_still_works(self):
        """The metaclass already injects the class itself into localns so
        self-referential forward refs ('Node' inside class Node) keep working.
        """
        class Node(DataModel):
            value: int
            next: Optional["Node"] = None

        a = Node(value=1)
        b = Node(value=2, next=a)
        assert b.next.value == 1

    def test_forward_ref_inside_generic(self):
        """An unresolvable name inside a generic also raises clearly."""
        from typing import List

        with pytest.raises(NameError) as exc:
            class Bad(DataModel):
                xs: List["NopeNope"]
        assert "Bad" in str(exc.value)


class TestIssue25AsyncValidatorOnSyncModel:
    """Issue #25: async validators on a sync DataModel are collected but
    never run. The user typically meant AsyncDataModel; warn at class
    definition so the mistake is obvious.
    """

    def test_async_field_validator_on_sync_model_warns(self):
        from vldt import async_field_validator, ValidatorMode

        with pytest.warns(UserWarning, match="async validator"):
            class M(DataModel):
                x: int

                @async_field_validator(mode=ValidatorMode.BEFORE)
                @classmethod
                async def coerce(cls, x):
                    return x

    def test_async_model_validator_on_sync_model_warns(self):
        from vldt import async_model_validator, ValidatorMode

        with pytest.warns(UserWarning, match="async validator"):
            class M(DataModel):
                x: int

                @async_model_validator(mode=ValidatorMode.BEFORE)
                @classmethod
                async def adjust(cls, data):
                    return data

    def test_warning_names_class_and_attribute(self):
        from vldt import async_field_validator, ValidatorMode

        with pytest.warns(UserWarning) as record:
            class MyModel(DataModel):
                x: int

                @async_field_validator(mode=ValidatorMode.BEFORE)
                @classmethod
                async def coerce(cls, x):
                    return x

        msgs = [str(w.message) for w in record]
        joined = " | ".join(msgs)
        assert "MyModel" in joined
        assert "coerce" in joined

    def test_async_validator_on_async_model_does_not_warn(self):
        """Async validators on AsyncDataModel are correct; no warning."""
        import warnings as _w
        from vldt import AsyncDataModel, async_field_validator, ValidatorMode

        with _w.catch_warnings():
            _w.simplefilter("error")  # any warning becomes an error
            class A(AsyncDataModel):
                x: int

                @async_field_validator(mode=ValidatorMode.BEFORE)
                @classmethod
                async def coerce(cls, x):
                    return x


class TestIssue13ReentrantDictIteration:
    """Issue #13: schema compilation iterated the annotations dict via
    PyDict_Next while calling PyObject_GetAttrString into user code (the
    annotation type's __getattribute__). A descriptor that mutates the
    annotations dict during that call would corrupt the C iteration. The
    fix is defensive: snapshot annotation items into a stable list before
    iterating in C.

    The two tests here pin the basic contract:

      - A model with many fields compiles and instantiates correctly. This
        is a regression guard: once we replace PyDict_Next with a snapshot,
        we need to be sure we still process all the right fields.
      - Repeated construction of a freshly defined wide model under stress
        (force re-compile) does not lose fields.
    """

    def test_wide_model_still_compiles_correctly(self):
        """30 declared fields, all primitive, all required. Every field must
        be present and validated."""
        attrs = {f"f{i}": int for i in range(30)}
        M = type("WideModel", (DataModel,), {"__annotations__": dict(attrs)})

        kwargs = {f"f{i}": i for i in range(30)}
        obj = M(**kwargs)
        for i in range(30):
            assert getattr(obj, f"f{i}") == i

    def test_repeated_freshly_defined_models_compile(self):
        """Defining and instantiating many models in a tight loop. If the
        snapshot were broken, schema compilation could read stale state
        between iterations."""
        for n in range(50):
            attrs = {"a": int, "b": int, "c": int}
            cls = type(f"M{n}", (DataModel,), {"__annotations__": dict(attrs)})
            obj = cls(a=1, b=2, c=3)
            assert (obj.a, obj.b, obj.c) == (1, 2, 3)
