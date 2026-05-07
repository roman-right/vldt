"""Tests pinning the validator hooks on the from_json one-pass path.

The validator paths used to fall back to materializing a Python dict and
calling cls(**dict). Now they run inside the schema-driven walk. These
tests exercise every validator hook through from_json and confirm the
behaviour matches the dict path.
"""

from typing import Any, List, Optional

import pytest

from vldt import (
    DataModel,
    Field,
    field_validator,
    model_validator,
    ValidatorMode,
)


class TestFieldBefore:
    def test_str_age_coerced_via_field_before(self):
        class M(DataModel):
            age: int

            @field_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def coerce(cls, age):
                if isinstance(age, str):
                    return int(age)
                return age

        assert M.from_json('{"age": "42"}').age == 42
        assert M.from_json('{"age": 42}').age == 42

    def test_field_before_only_runs_for_target_field(self):
        """A field_before on field A must NOT run on field B."""
        seen: list = []

        class M(DataModel):
            a: int
            b: int

            @field_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def watch_a(cls, a):
                seen.append(("a", a))
                return a

        M.from_json('{"a": 1, "b": 2}')
        assert seen == [("a", 1)]

    def test_field_before_default_value_path(self):
        """field_before runs against the default when the field is missing."""
        invocations: list = []

        class M(DataModel):
            x: int = 10

            @field_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def see(cls, x):
                invocations.append(x)
                return x

        assert M.from_json("{}").x == 10
        assert invocations == [10]

    def test_field_before_can_raise(self):
        class M(DataModel):
            age: int

            @field_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def must_be_string(cls, age):
                if not isinstance(age, str):
                    raise ValueError("age must arrive as string")
                return int(age)

        with pytest.raises(ValueError, match="must arrive as string"):
            M.from_json('{"age": 30}')


class TestModelBefore:
    def test_model_before_can_modify_dict(self):
        class M(DataModel):
            name: str

            @model_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def upper(cls, data):
                data["name"] = data["name"].upper()
                return data

        assert M.from_json('{"name": "alice"}').name == "ALICE"

    def test_model_before_can_inject_missing_field(self):
        class M(DataModel):
            name: str
            age: int

            @model_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def add_age(cls, data):
                data.setdefault("age", 99)
                return data

        obj = M.from_json('{"name": "x"}')
        assert obj.age == 99

    def test_model_before_runs_before_field_before(self):
        class M(DataModel):
            age: int

            @model_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def stringify(cls, data):
                data["age"] = "42"
                return data

            @field_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def coerce(cls, age):
                if isinstance(age, str):
                    return int(age)
                return age

        assert M.from_json('{"age": 1}').age == 42


class TestFieldAfter:
    def test_field_after_runs_after_validation(self):
        class M(DataModel):
            name: str

            @field_validator(mode=ValidatorMode.AFTER)
            @classmethod
            def cap(cls, name):
                return name.upper()

        assert M.from_json('{"name": "alice"}').name == "ALICE"


class TestModelAfter:
    def test_model_after_runs_on_instance(self):
        class M(DataModel):
            n: int

            @model_validator(mode=ValidatorMode.AFTER)
            def double(self):
                self.n *= 2

        assert M.from_json('{"n": 21}').n == 42


class TestCombinedHooks:
    def test_all_four_hooks_fire(self):
        order: list = []

        class M(DataModel):
            x: int

            @model_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def mb(cls, data):
                order.append("model_before")
                return data

            @field_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def fb(cls, x):
                order.append("field_before")
                return x

            @field_validator(mode=ValidatorMode.AFTER)
            @classmethod
            def fa(cls, x):
                order.append("field_after")
                return x

            @model_validator(mode=ValidatorMode.AFTER)
            def ma(self):
                order.append("model_after")

        M.from_json('{"x": 1}')
        assert order == [
            "model_before",
            "field_before",
            "field_after",
            "model_after",
        ]


class TestNestedModelValidators:
    def test_nested_model_validators_fire_through_json(self):
        seen = []

        class Inner(DataModel):
            x: int

            @field_validator(mode=ValidatorMode.AFTER)
            @classmethod
            def watch(cls, x):
                seen.append(x)
                return x

        class Outer(DataModel):
            inner: Inner

        Outer.from_json('{"inner": {"x": 7}}')
        assert seen == [7]

    def test_nested_model_field_before_runs(self):
        class Inner(DataModel):
            age: int

            @field_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def coerce(cls, age):
                if isinstance(age, str):
                    return int(age)
                return age

        class Outer(DataModel):
            inner: Inner

        obj = Outer.from_json('{"inner": {"age": "30"}}')
        assert obj.inner.age == 30

    def test_nested_model_model_before_runs(self):
        class Inner(DataModel):
            name: str

            @model_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def normalise(cls, data):
                data["name"] = data["name"].strip()
                return data

        class Outer(DataModel):
            inner: Inner

        obj = Outer.from_json('{"inner": {"name": "  alice  "}}')
        assert obj.inner.name == "alice"


class TestAliasesWithValidators:
    def test_field_before_with_alias(self):
        class M(DataModel):
            actual: int = Field(alias="given")

            @field_validator(mode=ValidatorMode.BEFORE)
            @classmethod
            def coerce(cls, actual):
                if isinstance(actual, str):
                    return int(actual)
                return actual

        obj = M.from_json('{"given": "5"}')
        assert obj.actual == 5
