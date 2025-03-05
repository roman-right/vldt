"""Tests for async validators and async data models."""

import json
import pytest
from typing import Any
from vldt import (
    AsyncDataModel,
    async_field_validator,
    async_model_validator,
    ValidatorMode,
)


class AsyncPerson(AsyncDataModel):
    """Async data model representing a person.

    Attributes:
        name (str): The person's name.
        age (int): The person's age.
    """

    name: str
    age: int

    @async_field_validator(mode=ValidatorMode.BEFORE)
    @classmethod
    async def check_age(cls, age: Any):
        """Validates and converts the age field before assignment.

        If age is an int, checks that it is not less than 18. If age is a string, converts it to an int after checking
        it contains only digits and validates it is not less than 18.

        Args:
            age (Any): The input age value.

        Returns:
            int: The validated and possibly converted age.

        Raises:
            ValueError: If age is invalid or less than 18.
        """
        if isinstance(age, int):
            if age < 18:
                raise ValueError("Person must be older than 18 years old")
        elif isinstance(age, str):
            if not age.isdigit():
                raise ValueError("Age must be a number")
            age = int(age)
            if age < 18:
                raise ValueError("Person must be older than 18 years old")
        else:
            raise ValueError("Invalid type for age")
        return age

    @async_field_validator(mode=ValidatorMode.AFTER)
    @classmethod
    async def check_name(cls, name: str):
        """Capitalizes the name field after assignment.

        Args:
            name (str): The input name value.

        Returns:
            str: The capitalized name.
        """
        return name.capitalize()

    @async_model_validator(mode=ValidatorMode.BEFORE)
    @classmethod
    async def check_model_before(cls, data: dict):
        """Ensures the name is capitalized in the data before model creation.

        Args:
            data (dict): The input data dictionary.

        Returns:
            dict: The modified data with a capitalized name.
        """
        if "name" in data:
            data["name"] = data["name"].capitalize()
        return data

    @async_model_validator(mode=ValidatorMode.AFTER)
    async def check_model_after(self):
        """Checks age and capitalizes name after model creation.

        Raises:
            ValueError: If age is less than 18.
        """
        if self.age < 18:
            raise ValueError("Person must be older than 18 years old")
        self.name = self.name.capitalize()


class AsyncProduct(AsyncDataModel):
    """Async data model representing a product.

    Attributes:
        name (str): The product name.
        price (float): The product price.
    """

    name: str
    price: float

    @async_field_validator(mode=ValidatorMode.BEFORE)
    @classmethod
    async def check_price(cls, price: Any):
        """Converts string price to float and verifies the price is non-negative.

        Args:
            price (Any): The input price value.

        Returns:
            float: The validated (and possibly converted) price.

        Raises:
            ValueError: If price cannot be converted to float or is negative.
        """
        if isinstance(price, str):
            try:
                price = float(price)
            except ValueError:
                raise ValueError("Price must be a number")
        if price < 0:
            raise ValueError("Price must be non-negative")
        return price

    @async_model_validator(mode=ValidatorMode.AFTER)
    async def adjust_price(self):
        """Rounds the price to two decimals after model creation."""
        self.price = round(self.price, 2)


class AsyncOrder(AsyncDataModel):
    """Async data model representing an order.

    Attributes:
        id (int): The order ID.
        total (float): The order total.
    """

    id: int
    total: float

    @async_model_validator(mode=ValidatorMode.AFTER)
    async def adjust_order(self):
        """Rounds the total to two decimals after model creation."""
        self.total = round(self.total, 2)


def create_invalid_async_validator_model():
    """Creates a model with an invalid async field validator signature.

    Returns:
        type: A dynamically created AsyncDataModel subclass with an invalid validator.
    """

    class InvalidAsyncModel(AsyncDataModel):
        """Async data model with an invalid field validator signature."""

        field: int

        @async_field_validator(mode=ValidatorMode.BEFORE)
        @classmethod
        async def invalid_validator(cls, a, b, c):
            """An invalid validator that accepts more than one field parameter."""
            return a

    return InvalidAsyncModel


class AsyncEmployee(AsyncDataModel):
    """Async data model representing an employee.

    Attributes:
        name (str): The employee's name.
        age (int): The employee's age.
    """

    name: str
    age: int

    @async_model_validator(mode=ValidatorMode.AFTER)
    async def instance_validator(self):
        """Converts the employee's name to uppercase after model creation."""
        self.name = self.name.upper()


@pytest.mark.asyncio
class TestAsyncPersonModel:
    """Tests for the AsyncPerson data model."""

    async def test_valid_person_with_int_age(self):
        """Test valid AsyncPerson creation when age is an int and name is lowercase."""
        p = await AsyncPerson(name="john", age=25)
        assert isinstance(p.age, int)
        assert p.age == 25
        assert p.name == "John"

    async def test_valid_person_with_str_age(self):
        """Test valid AsyncPerson creation when age is a string representing a valid integer."""
        p = await AsyncPerson(name="alice", age="30")
        assert isinstance(p.age, int)
        assert p.age == 30
        assert p.name == "Alice"

    async def test_invalid_person_age_non_numeric(self):
        """Test that providing a non-numeric string for age raises a ValueError."""
        with pytest.raises(ValueError, match="Age must be a number"):
            await AsyncPerson(name="bob", age="abc")

    async def test_invalid_person_age_underage(self):
        """Test that providing an underage value raises a ValueError."""
        with pytest.raises(ValueError, match="older than 18"):
            await AsyncPerson(name="tom", age="17")

    async def test_missing_field_raises_error(self):
        """Test that missing a required field (age) raises a TypeError."""
        with pytest.raises(TypeError):
            await AsyncPerson(name="dave")

    async def test_from_dict(self):
        """Test creating AsyncPerson from a dictionary."""
        data = {"name": "john", "age": 25}
        p = await AsyncPerson.from_dict(data)
        assert isinstance(p, AsyncPerson)
        assert p.name == "John"
        assert p.age == 25

    async def test_from_dict_with_invalid_data(self):
        """Test that creating AsyncPerson from a dict with invalid data raises a ValueError."""
        data = {"name": "john", "age": "abc"}
        with pytest.raises(ValueError, match="Age must be a number"):
            await AsyncPerson.from_dict(data)

    async def test_from_json(self):
        """Test creating AsyncPerson from a JSON string."""
        data = {"name": "john", "age": 25}
        json_str = json.dumps(data)
        p = await AsyncPerson.from_json(json_str)
        assert isinstance(p, AsyncPerson)
        assert p.name == "John"
        assert p.age == 25

    async def test_from_json_with_invalid_data(self):
        """Test that creating AsyncPerson from a JSON string with invalid data raises a ValueError."""
        data = {"name": "john", "age": "abc"}
        json_str = json.dumps(data)
        with pytest.raises(ValueError, match="Age must be a number"):
            await AsyncPerson.from_json(json_str)


@pytest.mark.asyncio
class TestAsyncProductModel:
    """Tests for the AsyncProduct data model."""

    async def test_valid_product(self):
        """Test valid AsyncProduct creation with price as a string representing a float."""
        prod = await AsyncProduct(name="widget", price="19.99")
        assert isinstance(prod.price, float)
        assert prod.price == 19.99

    async def test_negative_price(self):
        """Test that a negative price raises a ValueError."""
        with pytest.raises(ValueError, match="non-negative"):
            await AsyncProduct(name="gadget", price=-10)

    async def test_invalid_price_string(self):
        """Test that an invalid string for price raises a ValueError."""
        with pytest.raises(ValueError, match="Price must be a number"):
            await AsyncProduct(name="gizmo", price="free")


@pytest.mark.asyncio
class TestAsyncOrderModel:
    """Tests for the AsyncOrder data model."""

    async def test_model_validator_returns_dict(self):
        """Test that the AFTER model validator updates the order total correctly."""
        order = await AsyncOrder(id=1, total=123.4567)
        assert order.total == round(123.4567, 2)


@pytest.mark.asyncio
class TestAsyncModelValidatorInstanceMethod:
    """Tests for async model validator instance methods."""

    async def test_instance_method_validator(self):
        """Test that the instance validator for AsyncEmployee converts the name to uppercase."""
        emp = await AsyncEmployee(name="jane", age=30)
        assert emp.name == "JANE"


@pytest.mark.asyncio
class TestInvalidAsyncValidatorSignature:
    """Tests for models with invalid async validator signatures."""

    async def test_invalid_field_validator_signature(self):
        """Test that a model with an invalid async field validator signature raises a ValueError."""
        with pytest.raises(
            ValueError,
            match="Async field validator must have exactly one field parameter",
        ):
            create_invalid_async_validator_model()
