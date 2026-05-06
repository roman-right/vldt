"""Tests pinning the from_json one-pass path.

The JSON entry point now walks the rapidjson DOM with the model schema and
constructs the DataModel directly, skipping the intermediate Python dict.
These tests cover every behaviour that the dict-based path supports so the
new path cannot quietly diverge.
"""

import json
from datetime import datetime
from typing import Dict, List, Optional, Set, Tuple, Union
from uuid import UUID

import pytest

from vldt import (
    Config,
    DataModel,
    Field,
    field_validator,
    model_validator,
    ValidatorMode,
)


class TestPrimitiveTypes:
    def test_int_str_float_bool_none(self):
        class M(DataModel):
            i: int
            s: str
            f: float
            b: bool
            n: Optional[str]

        obj = M.from_json(
            '{"i": 42, "s": "hello", "f": 1.5, "b": true, "n": null}'
        )
        assert obj.i == 42
        assert obj.s == "hello"
        assert obj.f == 1.5
        assert obj.b is True
        assert obj.n is None

    def test_int_from_int_promotes_to_float(self):
        """JSON int written into a float field becomes a Python float."""

        class M(DataModel):
            x: float

        obj = M.from_json('{"x": 5}')
        assert obj.x == 5.0
        assert isinstance(obj.x, float)


class TestContainers:
    def test_list_of_int(self):
        class M(DataModel):
            xs: List[int]

        obj = M.from_json('{"xs": [1, 2, 3]}')
        assert obj.xs == [1, 2, 3]

    def test_dict_of_str_to_int(self):
        class M(DataModel):
            d: Dict[str, int]

        obj = M.from_json('{"d": {"a": 1, "b": 2}}')
        assert obj.d == {"a": 1, "b": 2}

    def test_tuple_with_size_check(self):
        class M(DataModel):
            t: Tuple[int, str, float]

        obj = M.from_json('{"t": [1, "two", 3.5]}')
        assert obj.t == (1, "two", 3.5)

        with pytest.raises(TypeError):
            M.from_json('{"t": [1, "two"]}')

    def test_set_of_int(self):
        class M(DataModel):
            s: Set[int]

        obj = M.from_json('{"s": [1, 2, 3]}')
        assert obj.s == {1, 2, 3}

    def test_deeply_nested_containers(self):
        class M(DataModel):
            d: Dict[str, List[Optional[int]]]

        obj = M.from_json('{"d": {"a": [1, null, 3]}}')
        assert obj.d == {"a": [1, None, 3]}


class TestUnions:
    def test_union_int_str(self):
        class M(DataModel):
            v: Union[int, str]

        assert M.from_json('{"v": 1}').v == 1
        assert M.from_json('{"v": "a"}').v == "a"

    def test_optional_field(self):
        class M(DataModel):
            v: Optional[int] = None

        assert M.from_json("{}").v is None
        assert M.from_json('{"v": null}').v is None
        assert M.from_json('{"v": 7}').v == 7

    def test_union_of_models(self):
        class A(DataModel):
            a: int

        class B(DataModel):
            b: str

        class C(DataModel):
            m: Union[A, B]

        obj1 = C.from_json('{"m": {"a": 1}}')
        assert isinstance(obj1.m, A)
        assert obj1.m.a == 1

        obj2 = C.from_json('{"m": {"b": "hi"}}')
        assert isinstance(obj2.m, B)
        assert obj2.m.b == "hi"


class TestNestedModels:
    def test_nested_model(self):
        class Inner(DataModel):
            x: int

        class Outer(DataModel):
            inner: Inner

        obj = Outer.from_json('{"inner": {"x": 5}}')
        assert obj.inner.x == 5

    def test_list_of_models(self):
        class Inner(DataModel):
            x: int

        class Outer(DataModel):
            items: List[Inner]

        obj = Outer.from_json('{"items": [{"x": 1}, {"x": 2}]}')
        assert [i.x for i in obj.items] == [1, 2]

    def test_dict_of_models(self):
        class Inner(DataModel):
            x: int

        class Outer(DataModel):
            mapping: Dict[str, Inner]

        obj = Outer.from_json('{"mapping": {"a": {"x": 1}, "b": {"x": 2}}}')
        assert obj.mapping["a"].x == 1
        assert obj.mapping["b"].x == 2


class TestDefaultsAndAliases:
    def test_default_value(self):
        class M(DataModel):
            a: int
            b: str = "hello"

        obj = M.from_json('{"a": 1}')
        assert obj.a == 1
        assert obj.b == "hello"

    def test_default_factory(self):
        class M(DataModel):
            xs: List[int] = Field(default_factory=list)

        obj = M.from_json("{}")
        assert obj.xs == []

    def test_single_alias(self):
        class M(DataModel):
            actual: str = Field(alias="alias_name")

        obj = M.from_json('{"alias_name": "value"}')
        assert obj.actual == "value"

    def test_multiple_aliases(self):
        class M(DataModel):
            actual: str = Field(alias=["a1", "a2"])

        assert M.from_json('{"a1": "x"}').actual == "x"
        assert M.from_json('{"a2": "y"}').actual == "y"

    def test_field_with_no_default_required(self):
        """Field() with no default must report a missing-field error."""

        class M(DataModel):
            a: int = Field()

        with pytest.raises(TypeError, match="Missing required field"):
            M.from_json("{}")

    def test_missing_required_reports_error(self):
        class M(DataModel):
            a: int

        with pytest.raises(TypeError, match="Missing required field"):
            M.from_json("{}")


class TestCustomDeserializers:
    def test_default_datetime(self):
        class M(DataModel):
            ts: datetime

        obj = M.from_json('{"ts": "2021-01-01T12:00:00"}')
        assert obj.ts == datetime(2021, 1, 1, 12, 0)

    def test_custom_datetime_deserializer(self):
        def parse(v: str) -> datetime:
            return datetime.strptime(v, "%Y/%m/%d %H:%M:%S")

        class M(DataModel):
            ts: datetime
            __vldt_config__ = Config(deserializer={datetime: {str: parse}})

        obj = M.from_json('{"ts": "2021/01/01 12:00:00"}')
        assert obj.ts == datetime(2021, 1, 1, 12, 0)

    def test_uuid(self):
        class M(DataModel):
            id: UUID

        obj = M.from_json(
            '{"id": "123e4567-e89b-12d3-a456-426614174000"}'
        )
        assert obj.id == UUID("123e4567-e89b-12d3-a456-426614174000")


class TestValidatorPaths:
    """Validator hooks must run regardless of which path from_json takes."""

    def test_field_after_validator(self):
        class M(DataModel):
            name: str

            @field_validator(mode=ValidatorMode.AFTER)
            @classmethod
            def cap(cls, name):
                return name.upper()

        obj = M.from_json('{"name": "alice"}')
        assert obj.name == "ALICE"

    def test_field_before_validator_falls_back(self):
        """field_before runs on the Python value, so the new path must
        materialize a dict for that branch and run the validator."""

        class M(DataModel):
            age: int

            @field_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def coerce(cls, age):
                if isinstance(age, str):
                    return int(age)
                return age

        obj = M.from_json('{"age": "42"}')
        assert obj.age == 42

    def test_model_before_validator_falls_back(self):
        class M(DataModel):
            name: str

            @model_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def upcase(cls, data):
                if "name" in data:
                    data["name"] = data["name"].upper()
                return data

        obj = M.from_json('{"name": "alice"}')
        assert obj.name == "ALICE"

    def test_model_after_validator(self):
        class M(DataModel):
            name: str

            @model_validator(mode=ValidatorMode.AFTER)
            def cap(self):
                self.name = self.name.title()

        obj = M.from_json('{"name": "alice smith"}')
        assert obj.name == "Alice Smith"


class TestErrorPaths:
    def test_invalid_json_raises_value_error(self):
        class M(DataModel):
            x: int

        with pytest.raises(ValueError):
            M.from_json("not json")

    def test_root_must_be_object(self):
        class M(DataModel):
            x: int

        with pytest.raises(TypeError):
            M.from_json("[1, 2, 3]")

    def test_type_mismatch_in_nested_field(self):
        class Inner(DataModel):
            x: int

        class Outer(DataModel):
            inner: Inner

        with pytest.raises(TypeError):
            Outer.from_json('{"inner": {"x": "not an int"}}')


class TestRoundTrip:
    def test_to_json_from_json_round_trip(self):
        class Inner(DataModel):
            x: int
            y: str

        class M(DataModel):
            i: Inner
            xs: List[int]
            d: Dict[str, str]
            opt: Optional[int] = None

        original = M(
            i=Inner(x=1, y="hi"),
            xs=[1, 2, 3],
            d={"k": "v"},
            opt=7,
        )
        round_tripped = M.from_json(original.to_json())
        assert round_tripped.to_dict() == original.to_dict()

    def test_byte_stable_round_trip(self):
        class M(DataModel):
            a: int
            b: str
            c: float

        original = M(a=1, b="x", c=1.5)
        s1 = original.to_json()
        s2 = M.from_json(s1).to_json()
        assert s1 == s2
