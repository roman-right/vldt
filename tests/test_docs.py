from datetime import datetime
from random import randint
from typing import Union, Optional

import pytest
from vldt import (
    AsyncDataModel,
    DataModel,
    Field,
    Config,
    ValidatorMode,
    field_validator,
    model_validator,
    async_field_validator,
    async_model_validator,
)


class UserProfile(DataModel):
    """User profile model.

    Attributes:
        username (str): Username.
        email (str): Email address.
        age (int): Age.
    """

    username: str
    email: str
    age: int


class UserProfileDefault(DataModel):
    """User profile model with a default country.

    Attributes:
        username (str): Username.
        email (str): Email address.
        country (str): Country. Defaults to "USA".
    """

    username: str
    email: str
    country: str = "USA"


class UserProfileOptional(DataModel):
    """User profile model with an optional phone number.

    Attributes:
        username (str): Username.
        phone (Optional[str]): Optional phone number.
    """

    username: str
    phone: Optional[str]


class Order(DataModel):
    """Order model with a default status.

    Attributes:
        order_id (int): Order identifier.
        status (str): Order status. Defaults to "pending".
    """

    order_id: int
    status: str = Field(default="pending")


class DailyLog(DataModel):
    """Daily log model with a dynamic default for entries.

    Attributes:
        date (str): The date.
        entries (list): List of log entries.
    """

    date: str
    entries: list = Field(default_factory=list)


class Session(DataModel):
    """Session model with a dynamically generated session ID.

    Attributes:
        session_id (int): Session ID generated dynamically.
    """

    session_id: int = Field(default_factory=lambda: randint(1000, 9999))


class SystemConfig(DataModel):
    """System configuration model with multiple field aliases.

    Attributes:
        timeout (int): Timeout value.
    """

    timeout: int = Field(default=30, alias=["timeout_seconds", "t_timeout"])


class SystemConfigSingleAlias(DataModel):
    """System configuration model with a single field alias.

    Attributes:
        timeout (int): Timeout value.
    """

    timeout: int = Field(default=30, alias="timeout_seconds")


class Product(DataModel):
    """Product model for testing round-trip serialization.

    Attributes:
        id (int): Product identifier.
        name (str): Product name.
        price (float): Product price.
    """

    id: int = Field(default=0)
    name: str = Field(default_factory=lambda: "Unknown")
    price: float = Field(default=0.0, alias="cost")


class Address(DataModel):
    """Address model.

    Attributes:
        street (str): Street address.
        zipcode (Union[int, str]): Zip code.
        country (str): Country. Defaults to "USA".
    """

    street: str
    zipcode: Union[int, str]
    country: str = "USA"


class OrderWithAddress(DataModel):
    """Order model with a nested shipping address.

    Attributes:
        order_id (int): Order identifier.
        items (list): List of items.
        shipping_address (Optional[Address]): Shipping address.
    """

    order_id: int
    items: list
    shipping_address: Address = None


class Inventory(DataModel):
    """Inventory model.

    Attributes:
        items (list): List of items.
        quantities (dict): Mapping of items to their quantities.
    """

    items: list
    quantities: dict


class Metrics(DataModel):
    """Metrics model.

    Attributes:
        dimensions (tuple): Dimensions as a tuple.
        unique_values (set): A set of unique float values.
    """

    dimensions: tuple
    unique_values: set


class User(DataModel):
    """User model with custom synchronous validators.

    Attributes:
        name (str): User name.
        age (int): Age.
    """

    name: str
    age: int

    @field_validator(mode=ValidatorMode.BEFORE)
    @classmethod
    def validate_age(cls, age):
        """Validate and convert age before assignment.

        Args:
            age (str|int): Age value.

        Returns:
            int: Validated age.

        Raises:
            ValueError: If age is invalid.
        """
        if isinstance(age, str):
            if not age.isdigit():
                raise ValueError("Age must be a number")
            age = int(age)
        if age < 18:
            raise ValueError("User must be at least 18 years old")
        return age

    @field_validator(mode=ValidatorMode.AFTER)
    @classmethod
    def format_name(cls, name):
        """Capitalize the name after assignment.

        Args:
            name (str): User name.

        Returns:
            str: Capitalized name.
        """
        return name.capitalize()

    @model_validator(mode=ValidatorMode.BEFORE)
    @classmethod
    def adjust_user(cls, data: dict):
        """Trim whitespace from the name field before validation.

        Args:
            data (dict): Input data.

        Returns:
            dict: Adjusted data.
        """
        if "name" in data:
            data["name"] = data["name"].strip()
        return data


async def get_minimum_age():
    """Simulate asynchronous retrieval of minimum age.

    Returns:
        int: The minimum age.
    """
    return 18


async def name_already_taken(name):
    """Simulate an asynchronous check if a name is already taken.

    Args:
        name (str): The name to check.

    Returns:
        bool: False, indicating the name is available.
    """
    return False


async def check_user_data(data: dict):
    """Simulate asynchronous user data validation.

    Args:
        data (dict): User data.

    Returns:
        bool: True if data is valid.
    """
    return True


class AsyncUser(AsyncDataModel):
    """Async user model with custom asynchronous validators.

    Attributes:
        name (str): User name.
        age (int): Age.
    """

    name: str
    age: int

    @async_field_validator(mode=ValidatorMode.BEFORE)
    @classmethod
    async def check_age(cls, age):
        """Asynchronously validate and convert age.

        Args:
            age (str|int): Age value.

        Returns:
            int: Validated age.

        Raises:
            ValueError: If age is below the minimum.
        """
        if isinstance(age, str):
            age = int(age)
        min_age = await get_minimum_age()
        if age < min_age:
            raise ValueError("User must be at least 18 years old")
        return age

    @async_field_validator(mode=ValidatorMode.AFTER)
    @classmethod
    async def check_name(cls, name):
        """Asynchronously validate the name.

        Args:
            name (str): User name.

        Returns:
            str: Validated name.

        Raises:
            ValueError: If the name is already taken.
        """
        if await name_already_taken(name):
            raise ValueError("Name already taken")
        return name

    @async_model_validator(mode=ValidatorMode.BEFORE)
    @classmethod
    async def adjust_user(cls, data: dict):
        """Asynchronously adjust user data.

        Args:
            data (dict): Input data.

        Returns:
            dict: Adjusted data.

        Raises:
            ValueError: If user data is invalid.
        """
        is_correct = await check_user_data(data)
        if not is_correct:
            raise ValueError("Invalid user data")
        return data


class AddressSerialization(DataModel):
    """Address model for serialization examples.

    Attributes:
        street (str): Street address.
        city (str): City.
        postal_code (str): Postal code.
    """

    street: str
    city: str
    postal_code: str


class CustomerOrder(DataModel):
    """Customer order model for serialization tests.

    Attributes:
        order_id (int): Order identifier.
        customer (str): Customer name.
        address (AddressSerialization): Customer address.
        notes (str): Order notes.
    """

    order_id: int
    customer: str
    address: AddressSerialization
    notes: str


def serialize_datetime(dt: datetime) -> str:
    """Serialize datetime to ISO format.

    Args:
        dt (datetime): Datetime object.

    Returns:
        str: ISO formatted string.
    """
    return dt.isoformat()


def deserialize_datetime(value: str) -> datetime:
    """Deserialize ISO formatted string to datetime.

    Args:
        value (str): ISO formatted string.

    Returns:
        datetime: Datetime object.
    """
    return datetime.fromisoformat(value)


class Event(DataModel):
    """Event model with custom datetime serialization.

    Attributes:
        name (str): Event name.
        start_time (datetime): Start time.
    """

    name: str
    start_time: datetime

    __vldt_config__ = Config(
        dict_serializer={datetime: serialize_datetime},
        json_serializer={datetime: serialize_datetime},
        deserializer={datetime: {str: deserialize_datetime}},
    )


def serialize_currency(amount: float) -> str:
    """Serialize float as a formatted currency string.

    Args:
        amount (float): Amount value.

    Returns:
        str: Formatted currency string.
    """
    return f"${amount:,.2f}"


def deserialize_currency(value: str) -> float:
    """Deserialize a currency string to a float.

    Args:
        value (str): Currency string.

    Returns:
        float: Numeric value.
    """
    return float(value.replace("$", "").replace(",", ""))


class Invoice(DataModel):
    """Invoice model with custom currency formatting.

    Attributes:
        invoice_number (int): Invoice number.
        total (float): Total amount.
    """

    invoice_number: int
    total: float

    __vldt_config__ = Config(
        dict_serializer={float: serialize_currency},
        json_serializer={float: serialize_currency},
        deserializer={float: {str: deserialize_currency}},
    )


class Warehouse(DataModel):
    """Warehouse model.

    Attributes:
        name (str): Warehouse name.
        location (str): Warehouse location.
    """

    name: str
    location: str


class InventoryItem(DataModel):
    """Inventory item model.

    Attributes:
        sku (str): SKU identifier.
        quantity (int): Quantity available.
    """

    sku: str
    quantity: int


class InventoryManagement(DataModel):
    """Inventory management model.

    Attributes:
        warehouses (dict): Mapping of warehouse identifiers to Warehouse data.
        items (list): List of inventory items.
    """

    warehouses: dict
    items: list


class ComplexModel(DataModel):
    """Complex model with class variables.

    Class Attributes:
        MAX_ITEMS (int): Maximum items allowed.
        TIMEOUT (float): Timeout duration.
    Attributes:
        id (int): Identifier.
        metadata (dict): Metadata.
        products (list): List of products.
        address (dict): Address data.
        history (list): History log.
    """

    MAX_ITEMS: int = 100
    TIMEOUT: float = 5.0

    id: int
    metadata: dict
    products: list
    address: dict = None
    history: list = []


class TestVLDTExamples:
    """Test suite covering VLDT usage examples from the documentation."""

    def test_basic_model_instantiation(self):
        """Test basic model instantiation (Section 4.1)."""
        profile = UserProfile(username="alice123", email="alice@example.com", age=28)
        assert profile.username == "alice123"
        assert profile.email == "alice@example.com"
        assert profile.age == 28

    def test_default_values(self):
        """Test default values for models (Section 4.2)."""
        profile = UserProfileDefault(username="bob", email="bob@example.com")
        assert profile.country == "USA"
        profile_opt = UserProfileOptional(username="carol", phone=None)
        assert profile_opt.phone is None

    def test_advanced_field_options_order_status(self):
        """Test Order model with default and overridden status (Section 4.3.1)."""
        order = Order(order_id=101)
        assert order.status == "pending"
        order2 = Order(order_id=102, status="shipped")
        assert order2.status == "shipped"

    def test_default_factory_daily_log_and_session(self):
        """Test DailyLog and Session models using default factories (Section 4.3.2)."""
        log1 = DailyLog(date="2025-03-01")
        log1.entries.append("User login")
        assert log1.entries == ["User login"]
        log2 = DailyLog(date="2025-03-02")
        assert log2.entries == []
        session1 = Session()
        session2 = Session()
        assert 1000 <= session1.session_id <= 9999
        assert 1000 <= session2.session_id <= 9999

    def test_field_aliasing(self):
        """Test field aliasing in SystemConfig models (Section 4.3.3)."""
        config = SystemConfig.from_dict({"t_timeout": 60})
        assert config.timeout == 60
        config_single = SystemConfigSingleAlias.from_dict({"timeout_seconds": 45})
        assert config_single.timeout == 45

    def test_round_trip_serialization(self):
        """Test round-trip serialization for Product model (Section 4.3.4)."""
        data = {"id": 12, "name": "Gadget", "cost": 19.99}
        product = Product.from_dict(data)
        product_dict = product.to_dict()
        assert product_dict == {"id": 12, "name": "Gadget", "price": 19.99}

    def test_union_types_and_nested_models(self):
        """Test nested models with union types (Section 4.4)."""
        address = Address(street="123 Main St", zipcode=90210)
        order = OrderWithAddress(
            order_id=1001, items=["book", "pen"], shipping_address=address
        )
        assert order.shipping_address.zipcode == 90210

    def test_collection_types(self):
        """Test container types in Inventory and Metrics models (Section 4.5)."""
        inventory = Inventory(
            items=["apple", "banana"], quantities={"apple": 10, "banana": 5}
        )
        assert inventory.items == ["apple", "banana"]
        assert inventory.quantities == {"apple": 10, "banana": 5}
        metrics = Metrics(dimensions=(1920, 1080), unique_values={3.14, 2.718})
        assert metrics.dimensions == (1920, 1080)
        assert isinstance(metrics.unique_values, set)

    def test_custom_validators_synchronous(self):
        """Test custom synchronous validators in User model (Section 4.6.1)."""
        user = User(name="  john  ", age="25")
        assert user.name == "John"
        assert user.age == 25
        with pytest.raises(ValueError):
            User(name="doe", age="16")

    @pytest.mark.asyncio
    async def test_custom_validators_asynchronous(self):
        """Test custom asynchronous validators in AsyncUser model (Section 4.6.2)."""
        user = await AsyncUser(name="alice", age="30")
        assert user.name == "alice"
        assert user.age == 30
        with pytest.raises(ValueError):
            await AsyncUser(name="bob", age="16")

    def test_serialization_deserialization_dict(self):
        """Test dictionary conversion in CustomerOrder model (Section 4.7 - Dictionary Conversion)."""
        order_data = {
            "order_id": 5001,
            "customer": "Dana",
            "address": {
                "street": "456 Elm St",
                "city": "Metropolis",
                "postal_code": "54321",
            },
            "notes": "Leave package at the door.",
        }
        order_obj = CustomerOrder.from_dict(order_data)
        order_dict = order_obj.to_dict()
        assert order_dict == order_data

    def test_serialization_deserialization_json(self):
        """Test JSON conversion in CustomerOrder model (Section 4.7 - JSON Conversion)."""
        order_data = {
            "order_id": 5001,
            "customer": "Dana",
            "address": {
                "street": "456 Elm St",
                "city": "Metropolis",
                "postal_code": "54321",
            },
            "notes": "Leave package at the door.",
        }
        order_obj = CustomerOrder.from_dict(order_data)
        json_str = order_obj.to_json()
        order_obj2 = CustomerOrder.from_json(json_str)
        assert order_obj2.to_dict() == order_data

    def test_custom_serialization_datetime(self):
        """Test custom datetime serialization and deserialization in Event model (Section 4.7 - Custom DateTime Handling)."""
        event = Event(name="Conference", start_time=datetime(2025, 3, 15, 9, 0))
        event_dict = event.to_dict()
        assert isinstance(event_dict["start_time"], str)
        json_str = event.to_json()
        event2 = Event.from_json(json_str)
        assert event2.name == "Conference"
        assert isinstance(event2.start_time, datetime)

    def test_custom_serialization_currency(self):
        """Test custom currency formatting in Invoice model (Section 4.7 - Custom Currency Formatting)."""
        invoice = Invoice(invoice_number=2025, total=1234.56)
        invoice_dict = invoice.to_dict()
        assert isinstance(invoice_dict["total"], str)
        json_invoice = invoice.to_json()
        invoice2 = Invoice.from_json(json_invoice)
        assert invoice2.invoice_number == 2025
        assert abs(invoice2.total - 1234.56) < 0.001

    def test_advanced_nested_structures_inventory_management(self):
        """Test deeply nested structures in InventoryManagement model (Section 4.8)."""
        inventory_data = {
            "warehouses": {
                "WH1": {"name": "Main Warehouse", "location": "New York"},
                "WH2": {"name": "Secondary Warehouse", "location": "Los Angeles"},
            },
            "items": [
                {"sku": "A123", "quantity": 100},
                {"sku": "B456", "quantity": 200},
            ],
        }
        inventory = InventoryManagement.from_dict(inventory_data)
        assert inventory.warehouses["WH2"]["location"] == "Los Angeles"

    def test_advanced_complex_model_with_class_vars(self):
        """Test ComplexModel with class variables (Section 4.8 - Complex Model with Class Variables)."""
        complex_obj = ComplexModel(
            id=1,
            metadata={"version": 1.0},
            products=[{"id": 1, "name": "Widget", "price": 9.99}],
            address={"street": "123 Main St", "zipcode": 90210},
        )
        assert complex_obj.id == 1
        assert complex_obj.metadata["version"] == 1.0
        assert complex_obj.products[0]["name"] == "Widget"
        assert complex_obj.address["zipcode"] == 90210
