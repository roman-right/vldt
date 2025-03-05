from datetime import datetime
from uuid import UUID

from vldt import DataModel, Config


def from_string(v: str) -> datetime:
    """Convert a date string in 'YYYY/MM/DD HH:MM:SS' format to a datetime object.

    Args:
        v (str): The date string to convert.

    Returns:
        datetime: The corresponding datetime object.
    """
    return datetime.strptime(v, "%Y/%m/%d %H:%M:%S")


class ModelWithManyTypes(DataModel):
    """Data model representing various types with custom deserialization.

    Attributes:
        id (UUID): The unique identifier.
        name (str): The name of the entity.
        age (int): The age value.
        height (float): The height measurement.
        is_active (bool): Flag indicating active status.
        created_at (datetime): The creation datetime.
    """

    id: UUID
    name: str
    age: int
    height: float
    is_active: bool
    created_at: datetime

    __vldt_config__ = Config(
        deserializer={
            datetime: {
                str: from_string,
            }
        }
    )


class TestDeserializer:
    """Test cases for the ModelWithManyTypes data model deserialization."""

    def test_init(self):
        """Test the initialization of ModelWithManyTypes with direct parameters."""
        obj = ModelWithManyTypes(
            id=UUID("123e4567-e89b-12d3-a456-426614174000"),
            name="Alice",
            age=30,
            height=1.75,
            is_active=True,
            created_at="2021/01/01 12:00:00",
        )
        assert obj.id == UUID("123e4567-e89b-12d3-a456-426614174000")
        assert obj.name == "Alice"
        assert obj.age == 30
        assert obj.height == 1.75
        assert obj.is_active is True
        assert obj.created_at == datetime(2021, 1, 1, 12, 0)

    def test_from_dict(self):
        """Test the creation of ModelWithManyTypes from a dictionary."""
        data = {
            "id": "123e4567-e89b-12d3-a456-426614174000",
            "name": "Alice",
            "age": 30,
            "height": 1.75,
            "is_active": True,
            "created_at": "2021/01/01 12:00:00",
        }
        obj = ModelWithManyTypes.from_dict(data)
        assert obj.id == UUID("123e4567-e89b-12d3-a456-426614174000")
        assert obj.name == "Alice"
        assert obj.age == 30
        assert obj.height == 1.75
        assert obj.is_active is True
        assert obj.created_at == datetime(2021, 1, 1, 12, 0)
