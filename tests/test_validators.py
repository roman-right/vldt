"""Module containing data models and pytest test cases for validating them using vldt."""

import pytest
from typing import Any
from vldt import DataModel, field_validator, model_validator, ValidatorMode


class Person(DataModel):
    """Data model representing a person.

    Attributes:
        name (str): The person's name.
        age (int): The person's age.
    """

    name: str
    age: int

    @field_validator(mode=ValidatorMode.BEFORE)
    @classmethod
    def check_age(cls, age: Any):
        """Convert a string to an int and validate that age is at least 18.

        Args:
            age (Any): The age value to validate.

        Returns:
            int: The validated age as an integer.

        Raises:
            ValueError: If the age is below 18, not a number, or of an invalid type.
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

    @field_validator(mode=ValidatorMode.AFTER)
    @classmethod
    def check_name(cls, name: str):
        """Capitalize the name after field validation.

        Args:
            name (str): The name to capitalize.

        Returns:
            str: The capitalized name.
        """
        return name.capitalize()

    @model_validator(mode=ValidatorMode.BEFORE)
    @classmethod
    def check_model_before(cls, data: dict):
        """Capitalize the name in the data dictionary before model validation.

        Args:
            data (dict): The data dictionary containing model fields.

        Returns:
            dict: The updated data dictionary with the capitalized name.
        """
        if "name" in data:
            data["name"] = data["name"].capitalize()
        return data

    @model_validator(mode=ValidatorMode.AFTER)
    def check_model_after(self):
        """Revalidate age and ensure the name is capitalized after model validation.

        Raises:
            ValueError: If the age is less than 18.
        """
        if self.age < 18:
            raise ValueError("Person must be older than 18 years old")
        self.name = self.name.capitalize()


class Product(DataModel):
    """Data model representing a product.

    Attributes:
        name (str): The product name.
        price (float): The product price.
    """

    name: str
    price: float

    @field_validator(mode=ValidatorMode.BEFORE)
    @classmethod
    def check_price(cls, price: Any):
        """Convert a string price to float and ensure the price is non-negative.

        Args:
            price (Any): The price value to validate.

        Returns:
            float: The validated price as a float.

        Raises:
            ValueError: If the price cannot be converted to a float or is negative.
        """
        if isinstance(price, str):
            try:
                price = float(price)
            except ValueError:
                raise ValueError("Price must be a number")
        if price < 0:
            raise ValueError("Price must be non-negative")
        return price

    @model_validator(mode=ValidatorMode.AFTER)
    def adjust_price(self):
        """Round the price to two decimals after model validation."""
        self.price = round(self.price, 2)


class Order(DataModel):
    """Data model representing an order.

    Attributes:
        id (int): The order ID.
        total (float): The total amount of the order.
    """

    id: int
    total: float

    @model_validator(mode=ValidatorMode.AFTER)
    def adjust_order(self):
        """Round the total amount to two decimals after model validation."""
        self.total = round(self.total, 2)


def create_invalid_validator_model():
    """Create a data model with an invalid field validator signature.

    Returns:
        type: A DataModel subclass with an incorrectly defined field validator.
    """

    class InvalidModel(DataModel):
        """Data model with an invalid field validator for testing purposes.

        Attributes:
            field (int): A sample field.
        """

        field: int

        @field_validator(mode=ValidatorMode.BEFORE)
        @classmethod
        def invalid_validator(cls, a, b, c):
            """Invalid field validator with wrong number of parameters.

            Args:
                a: First parameter.
                b: Second parameter.
                c: Third parameter.

            Returns:
                Any: The first parameter.
            """
            return a

    return InvalidModel


class Employee(DataModel):
    """Data model representing an employee.

    Attributes:
        name (str): The employee's name.
        age (int): The employee's age.
    """

    name: str
    age: int

    @model_validator(mode=ValidatorMode.AFTER)
    def instance_validator(self):
        """Convert the employee's name to uppercase after model validation."""
        self.name = self.name.upper()


class TestPersonModel:
    """Test cases for the Person model."""

    def test_valid_person_with_int_age(self):
        """Test that a valid person with integer age is correctly processed."""
        p = Person(name="john", age=25)
        assert isinstance(p.age, int)
        assert p.age == 25
        assert p.name == "John"

    def test_valid_person_with_str_age(self):
        """Test that a valid person with age provided as a string is correctly processed."""
        p = Person(name="alice", age="30")
        assert isinstance(p.age, int)
        assert p.age == 30
        assert p.name == "Alice"

    def test_invalid_person_age_non_numeric(self):
        """Test that a non-numeric age string raises a ValueError."""
        with pytest.raises(ValueError, match="Age must be a number"):
            Person(name="bob", age="abc")

    def test_invalid_person_age_underage(self):
        """Test that an underage person raises a ValueError."""
        with pytest.raises(ValueError, match="older than 18"):
            Person(name="tom", age="17")

    def test_missing_field_raises_error(self):
        """Test that missing a required field raises a TypeError."""
        with pytest.raises(TypeError):
            Person(name="dave")  # 'age' is missing


class TestProductModel:
    """Test cases for the Product model."""

    def test_valid_product(self):
        """Test that a valid product with price as a string is correctly processed."""
        prod = Product(name="widget", price="19.99")
        assert isinstance(prod.price, float)
        assert prod.price == 19.99

    def test_negative_price(self):
        """Test that a negative price raises a ValueError."""
        with pytest.raises(ValueError, match="non-negative"):
            Product(name="gadget", price=-10)

    def test_invalid_price_string(self):
        """Test that an invalid price string raises a ValueError."""
        with pytest.raises(ValueError, match="Price must be a number"):
            Product(name="gizmo", price="free")


class TestOrderModel:
    """Test cases for the Order model."""

    def test_model_validator_returns_dict(self):
        """Test that the order total is correctly rounded after model validation."""
        order = Order(id=1, total=123.4567)
        assert order.total == round(123.4567, 2)


class TestModelValidatorInstanceMethod:
    """Test cases for models with instance method validators."""

    def test_instance_method_validator(self):
        """Test that an instance method validator correctly processes the Employee model."""
        emp = Employee(name="jane", age=30)
        assert emp.name == "JANE"


class TestInvalidValidatorSignature:
    """Test cases for models with invalid validator signatures."""

    def test_invalid_field_validator_signature(self):
        """Test that defining a model with an incorrect field validator signature raises a ValueError."""
        with pytest.raises(
            ValueError, match="Field validator must have exactly one field parameter"
        ):
            create_invalid_validator_model()
