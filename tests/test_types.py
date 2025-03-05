from datetime import datetime
from uuid import UUID

from vldt import DataModel


class ModelWithManyTypes(DataModel):
    """Data model representing an object with multiple types.

    Attributes:
        id (UUID): Unique identifier.
        name (str): Name of the model.
        age (int): Age of the model.
        height (float): Height value.
        is_active (bool): Active status flag.
        created_at (datetime): Timestamp when the model was created.
    """

    id: UUID
    name: str
    age: int
    height: float
    is_active: bool
    created_at: datetime


class TestTypes:
    """Test suite for ModelWithManyTypes."""

    def test_init(self):
        """Test initialization of ModelWithManyTypes.

        This test creates an instance of ModelWithManyTypes with predefined values
        and asserts that all attributes are correctly assigned.
        """
        obj = ModelWithManyTypes(
            id=UUID("123e4567-e89b-12d3-a456-426614174000"),
            name="Alice",
            age=30,
            height=1.75,
            is_active=True,
            created_at=datetime(2021, 1, 1, 12, 0),
        )
        assert obj.id == UUID("123e4567-e89b-12d3-a456-426614174000")
        assert obj.name == "Alice"
        assert obj.age == 30
        assert obj.height == 1.75
        assert obj.is_active is True
        assert obj.created_at == datetime(2021, 1, 1, 12, 0)

    def test_from_dict(self):
        """Test creation of ModelWithManyTypes from a dictionary.

        This test verifies that the from_dict method correctly converts a dictionary
        into an instance of ModelWithManyTypes with the expected attribute values.
        """
        data = {
            "id": "123e4567-e89b-12d3-a456-426614174000",
            "name": "Alice",
            "age": 30,
            "height": 1.75,
            "is_active": True,
            "created_at": "2021-01-01T12:00:00",
        }
        obj = ModelWithManyTypes.from_dict(data)
        assert obj.id == UUID("123e4567-e89b-12d3-a456-426614174000")
        assert obj.name == "Alice"
        assert obj.age == 30
        assert obj.height == 1.75
        assert obj.is_active is True
        assert obj.created_at == datetime(2021, 1, 1, 12, 0)
