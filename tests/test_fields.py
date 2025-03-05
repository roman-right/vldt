from random import randint

import pytest
from vldt import DataModel, Field


class LiteralDefaultModel(DataModel):
    """Data model with a literal default value for field x.

    Attributes:
        x (int): Field with a literal default value of 42.
    """

    x: int = Field(default=42)


class DefaultFactoryModel(DataModel):
    """Data model with a default factory for field lst.

    Attributes:
        lst (list): Field with a default factory that returns a new empty list.
    """

    lst: list = Field(default_factory=list)


class AliasModel(DataModel):
    """Data model that uses a literal default and an alias for field s.

    Attributes:
        s (str): Field with a literal default value "default" and an alias "alias_s".
    """

    s: str = Field(default="default", alias="alias_s")


class MissingFieldModel(DataModel):
    """Data model with a required field that has no default.

    Attributes:
        a (int): Field without a default value; its absence should trigger a validation error.
    """

    a: int = Field()


class RoundTripModel(DataModel):
    """Data model combining literal default, default factory, and alias.

    Attributes:
        a (int): Field with a literal default value of 10.
        b (str): Field with a default factory that returns "factory".
        c (float): Field with a literal default value of 3.14 and an alias "c_alias".
    """

    a: int = Field(default=10)
    b: str = Field(default_factory=lambda: "factory")
    c: float = Field(default=3.14, alias="c_alias")


class TestFieldBehavior:
    """Test cases for field behavior in DataModel implementations."""

    def test_literal_default(self):
        """Test that LiteralDefaultModel uses the literal default for x when no value is provided."""
        m = LiteralDefaultModel()
        assert m.x == 42

        m2 = LiteralDefaultModel(x=100)
        assert m2.x == 100

    def test_default_factory(self):
        """Test that DefaultFactoryModel uses its default factory to provide an empty list."""
        m = DefaultFactoryModel()
        assert m.lst == []

        m1 = DefaultFactoryModel()
        m2 = DefaultFactoryModel()
        m1.lst.append(1)
        assert m1.lst == [1]
        assert m2.lst == []

    def test_default_factory_with_random(self):
        """Test that RandomModel uses a default factory producing a random integer.

        The test ensures that different instances have different random values and that the values are integers.
        """

        class RandomModel(DataModel):
            """Data model with a random integer field using a default factory.

            Attributes:
                i (int): Field with a default factory that returns a random integer between 0 and 100.
            """

            i: int = Field(default_factory=lambda: randint(0, 100))

        r1 = RandomModel()
        r2 = RandomModel()
        assert r1.i != r2.i
        assert isinstance(r1.i, int)
        assert isinstance(r2.i, int)

    def test_alias(self):
        """Test that AliasModel correctly handles alias input and literal default value."""
        m = AliasModel()
        assert m.s == "default"

        m2 = AliasModel.from_dict({"alias_s": "provided"})
        assert m2.s == "provided"

    def test_multiple_aliases(self):
        """Test that a model with multiple aliases for a field correctly maps the provided alias value.

        The test checks that any of the provided aliases is accepted and that the first occurrence is used.
        """

        class MultipleAliasModel(DataModel):
            """Data model with multiple aliases for field s.

            Attributes:
                s (str): Field with aliases "alias1" and "alias2".
            """

            s: str = Field(alias=["alias1", "alias2"])

        m = MultipleAliasModel.from_dict({"alias1": "value1"})
        assert m.s == "value1"

        m2 = MultipleAliasModel.from_dict({"alias2": "value2"})
        assert m2.s == "value2"

        m3 = MultipleAliasModel.from_dict({"alias1": "value1", "alias2": "value2"})
        assert m3.s == "value1"

    def test_missing_field(self):
        """Test that MissingFieldModel raises an error when a required field is missing."""
        with pytest.raises(Exception):
            MissingFieldModel.from_dict({})

    def test_round_trip_fields(self):
        """Test that RoundTripModel correctly converts to and from dictionaries using aliases.

        The test verifies that the model uses the alias during input conversion and outputs the canonical field name.
        """
        data = {"a": 5, "b": "override", "c_alias": 2.71}
        m = RoundTripModel.from_dict(data)
        d = m.to_dict()
        expected = {"a": 5, "b": "override", "c": 2.71}
        assert d == expected
