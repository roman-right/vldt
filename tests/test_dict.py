from typing import List, Dict, Optional, Union

import pytest

from vldt import DataModel
from vldt.config import Config


class Address(DataModel):
    """Data model representing an address.

    Attributes:
        street (str): The street address.
        city (str): The city name.
        postal_code (str): The postal or ZIP code.
    """

    street: str
    city: str
    postal_code: str


class Company(DataModel):
    """Data model representing a company.

    Attributes:
        name (str): The name of the company.
        industry (str): The industry in which the company operates.
        employees (int): The number of employees.
    """

    name: str
    industry: str
    employees: int


class User(DataModel):
    """Data model representing a user.

    Attributes:
        id (int): The unique identifier for the user.
        name (str): The name of the user.
        age (int): The age of the user.
        active (bool): The user's active status.
        address (Address): The address associated with the user.
        notes (Optional[str]): Additional notes about the user.
    """

    id: int
    name: str
    age: int
    active: bool
    address: Address
    notes: Optional[str]


class CollectionModel(DataModel):
    """Data model with list and dict fields containing nested models.

    Attributes:
        items (List[int]): A list of integers.
        mapping (Dict[str, Address]): A dictionary mapping strings to Address objects.
    """

    items: List[int]
    mapping: Dict[str, Address]


def custom_float_serializer(v: float) -> str:
    """Custom serializer that converts a float to a string rounded to 2 decimals.

    Args:
        v (float): The float value to serialize.

    Returns:
        str: The float formatted as a string rounded to two decimal places.
    """
    return f"{v:.2f}"


class ConfigModel(DataModel):
    """Data model with a custom configuration for serializing float values.

    Attributes:
        value (float): A float value that will be serialized using a custom serializer.
    """

    value: float
    __vldt_config__ = Config(dict_serializer={float: custom_float_serializer})


class TestToDictFromDict:
    """Test cases for converting models to dictionaries and back."""

    def test_simple_to_dict(self):
        """Test converting a simple User model to a dictionary."""
        addr = Address(street="Main St", city="Town", postal_code="12345")
        user = User(id=1, name="Alice", age=30, active=True, address=addr, notes=None)
        d = user.to_dict()
        expected = {
            "id": 1,
            "name": "Alice",
            "age": 30,
            "active": True,
            "address": {"street": "Main St", "city": "Town", "postal_code": "12345"},
            "notes": None,
        }
        assert d == expected

    def test_from_dict_round_trip(self):
        """Test round-trip conversion from dictionary to User model and back."""
        data = {
            "id": 2,
            "name": "Bob",
            "age": 40,
            "active": False,
            "address": {
                "street": "2nd Ave",
                "city": "Cityville",
                "postal_code": "54321",
            },
            "notes": "Test note",
        }
        user = User.from_dict(data)
        d = user.to_dict()
        assert d == data

    def test_nested_models(self):
        """Test conversion of a model with nested models."""

        class Employee(DataModel):
            """Data model representing an employee with nested models.

            Attributes:
                name (str): The employee's name.
                age (int): The employee's age.
                address (Address): The employee's address.
                company (Company): The company where the employee works.
            """

            name: str
            age: int
            address: Address
            company: Company

        emp_data = {
            "name": "Carol",
            "age": 28,
            "address": {
                "street": "Third St",
                "city": "Village",
                "postal_code": "11111",
            },
            "company": {"name": "Acme", "industry": "Manufacturing", "employees": 100},
        }
        emp = Employee.from_dict(emp_data)
        d = emp.to_dict()
        assert d == emp_data

    def test_list_and_dict_fields(self):
        """Test conversion of a model with list and dictionary fields."""
        data = {
            "items": [1, 2, 3, 4],
            "mapping": {
                "home": {"street": "Fourth St", "city": "Metro", "postal_code": "22222"}
            },
        }
        coll = CollectionModel.from_dict(data)
        d = coll.to_dict()
        assert d == data

    def test_custom_config_serializer(self):
        """Test that the custom configuration serializer correctly serializes float values."""
        model = ConfigModel(value=3.14159)
        d = model.to_dict()
        expected = {"value": "3.14"}
        assert d == expected

    def test_missing_fields(self):
        """Test that missing required fields in the input dictionary raise an exception."""
        data = {
            "id": 5,
            "name": "Dave",
            "active": True,
            "address": {
                "street": "Fifth Ave",
                "city": "Big City",
                "postal_code": "33333",
            },
            "notes": None,
        }
        with pytest.raises(Exception):
            User.from_dict(data)

    def test_extra_fields(self):
        """Test handling of extra fields in the input dictionary.

        Extra fields should either be ignored or cause an exception depending on the implementation.
        """
        data = {
            "id": 6,
            "name": "Eve",
            "age": 35,
            "active": True,
            "address": {
                "street": "Sixth St",
                "city": "Small Town",
                "postal_code": "44444",
            },
            "notes": "Extra",
            "extra": "should be ignored",
        }
        try:
            user = User.from_dict(data)
            d = user.to_dict()
            expected = data.copy()
            expected.pop("extra", None)
            assert d == expected
        except Exception:
            pass

    def test_none_handling(self):
        """Test that fields with None values are preserved during conversion."""
        data = {
            "id": 7,
            "name": "Frank",
            "age": 50,
            "active": False,
            "address": {
                "street": "Seventh St",
                "city": "Nowhere",
                "postal_code": "55555",
            },
            "notes": None,
        }
        user = User.from_dict(data)
        d = user.to_dict()
        assert d == data

    def test_union_of_base_models(self):
        """Test handling of Union types in base models.

        This test ensures that a field accepting a union of models is properly instantiated.
        """

        class A(DataModel):
            """Data model representing type A.

            Attributes:
                a (int): An integer attribute for type A.
            """

            a: int

        class B(DataModel):
            """Data model representing type B.

            Attributes:
                b (str): A string attribute for type B.
            """

            b: str

        class C(DataModel):
            """Data model that uses a Union of two base models.

            Attributes:
                c (float): A float attribute.
                m (Union[A, B]): A field that can be either type A or B.
            """

            c: float
            m: Union[A, B]

        obj = C.from_dict({"c": 1.0, "m": {"a": 1}})
        assert obj.c == 1.0
        assert obj.m.a == 1
        assert isinstance(obj.m, A)

        obj = C.from_dict({"c": 1.0, "m": {"b": "test"}})
        assert obj.c == 1.0
        assert obj.m.b == "test"
        assert isinstance(obj.m, B)
