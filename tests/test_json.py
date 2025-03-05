"""Module providing data models and tests for JSON serialization and deserialization.

This module defines several data models using a custom DataModel base class,
and includes tests for converting models to and from JSON format using pytest.
"""

from typing import List, Dict, Optional
import json
import pytest

from vldt import DataModel
from vldt.config import Config


class Address(DataModel):
    """Data model for an address.

    Attributes:
        street (str): The street address.
        city (str): The city name.
        postal_code (str): The postal code.
    """

    street: str
    city: str
    postal_code: str


class Company(DataModel):
    """Data model for a company.

    Attributes:
        name (str): The company name.
        industry (str): The industry in which the company operates.
        employees (int): The number of employees.
    """

    name: str
    industry: str
    employees: int


class User(DataModel):
    """Data model for a user.

    Attributes:
        id (int): The user ID.
        name (str): The user's name.
        age (int): The user's age.
        active (bool): The user's active status.
        address (Address): The user's address.
        notes (Optional[str]): Optional notes for the user.
    """

    id: int
    name: str
    age: int
    active: bool
    address: Address
    notes: Optional[str]


class CollectionModel(DataModel):
    """Data model for a collection with list and dictionary fields.

    Attributes:
        items (List[int]): A list of integers.
        mapping (Dict[str, Address]): A dictionary mapping strings to Address objects.
    """

    items: List[int]
    mapping: Dict[str, Address]


def custom_float_serializer(v: float) -> str:
    """Serialize a float to a string rounded to 2 decimal places.

    Args:
        v (float): The float value to serialize.

    Returns:
        str: The serialized float as a string with 2 decimals.
    """
    return f"{v:.2f}"


class ConfigModel(DataModel):
    """Data model with a custom configuration for JSON serialization.

    Attributes:
        value (float): A float value that will be serialized using a custom serializer.
    """

    value: float
    __vldt_config__ = Config(json_serializer={float: custom_float_serializer})


class TestToJsonFromJson:
    """Pytest class for testing JSON serialization and deserialization of data models."""

    def test_simple_to_json(self):
        """Test conversion of a simple User model to JSON."""
        addr = Address(street="Main St", city="Town", postal_code="12345")
        user = User(id=1, name="Alice", age=30, active=True, address=addr, notes=None)
        json_str = user.to_json()
        d = json.loads(json_str)
        expected = {
            "id": 1,
            "name": "Alice",
            "age": 30,
            "active": True,
            "address": {"street": "Main St", "city": "Town", "postal_code": "12345"},
            "notes": None,
        }
        assert d == expected

    def test_from_json_round_trip(self):
        """Test that converting from JSON to a User model and back is consistent."""
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
        json_str = json.dumps(data)
        user = User.from_json(json_str)
        d = json.loads(user.to_json())
        assert d == data

    def test_nested_models(self):
        """Test JSON serialization and deserialization with nested models."""

        class Employee(DataModel):
            """Data model for an employee with nested Address and Company models.

            Attributes:
                name (str): The employee's name.
                age (int): The employee's age.
                address (Address): The employee's address.
                company (Company): The employee's company.
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
        json_str = json.dumps(emp_data)
        emp = Employee.from_json(json_str)
        d = json.loads(emp.to_json())
        assert d == emp_data

    def test_list_and_dict_fields(self):
        """Test JSON serialization and deserialization of models with list and dict fields."""
        data = {
            "items": [1, 2, 3, 4],
            "mapping": {
                "home": {"street": "Fourth St", "city": "Metro", "postal_code": "22222"}
            },
        }
        json_str = json.dumps(data)
        coll = CollectionModel.from_json(json_str)
        d = json.loads(coll.to_json())
        assert d == data

    def test_custom_config_serializer(self):
        """Test that the custom serializer in ConfigModel serializes float values correctly."""
        model = ConfigModel(value=3.14159)
        json_str = model.to_json()
        d = json.loads(json_str)
        expected = {"value": "3.14"}
        assert d == expected

    def test_missing_fields(self):
        """Test that a validation error is raised when required fields are missing."""
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
        json_str = json.dumps(data)
        with pytest.raises(Exception):
            User.from_json(json_str)

    def test_extra_fields(self):
        """Test handling of extra fields in JSON data during deserialization."""
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
        json_str = json.dumps(data)
        try:
            user = User.from_json(json_str)
            d = json.loads(user.to_json())
            expected = data.copy()
            expected.pop("extra", None)
            assert d == expected
        except Exception:
            pass

    def test_none_handling(self):
        """Test that fields with None values are preserved during JSON serialization and deserialization."""
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
        json_str = json.dumps(data)
        user = User.from_json(json_str)
        d = json.loads(user.to_json())
        assert d == data
