import copy
from typing import ClassVar, Union, Optional, List, Dict, Any

import pytest

from tests.conftest import type_error_to_dict
from vldt import DataModel, Field


class Address(DataModel):
    """Represents an address.

    Attributes:
        street (str): The street address.
        zipcode (Union[int, str]): The postal code.
        country (str): The country. Defaults to "USA".
    """

    street: str
    zipcode: Union[int, str]
    country: str = "USA"


class Product(DataModel):
    """Represents a product.

    Attributes:
        id (int): The product identifier.
        name (str): The name of the product.
        price (float): The price of the product.
        in_stock (bool): Availability status. Defaults to True.
    """

    id: int
    name: str
    price: float
    in_stock: bool = True


class ProductWithStrId(DataModel):
    """Represents a product with a string identifier.

    Attributes:
        id (str): The product identifier.
        name (str): The name of the product.
        price (float): The price of the product.
        in_stock (bool): Availability status. Defaults to True.
    """

    id: str
    name: str
    price: float
    in_stock: bool = True


class ComplexModel(DataModel):
    """Represents a complex model with various nested structures.

    Class Attributes:
        MAX_ITEMS (int): Maximum number of items.
        TIMEOUT (float): Timeout duration in seconds.

    Attributes:
        id (Union[int, str]): Identifier.
        metadata (Dict[str, Any]): Metadata dictionary.
        products (List[Product]): List of Product instances.
        address (Optional[Address]): Optional address.
        history (List[Union[int, Dict[str, float]]]): History data.
    """

    MAX_ITEMS: ClassVar[int] = 100
    TIMEOUT: ClassVar[float] = 5.0

    id: Union[int, str]
    metadata: Dict[str, Any]
    products: List[Product]
    address: Optional[Address] = None
    history: List[Union[int, Dict[str, float]]] = []


class TestDataModelValidation:
    """Test suite for validating DataModel behaviors."""

    def test_basic_valid_instantiation(self):
        """Test basic valid model creation.

        Raises:
            AssertionError: If attribute values are not set as expected.
        """

        class SimpleModel(DataModel):
            """A simple model for testing instantiation.

            Attributes:
                name (str): The name.
                age (int): The age.
            """

            name: str
            age: int

        obj = SimpleModel(name="Alice", age=30)
        assert obj.name == "Alice"
        assert obj.age == 30

    def test_invalid_type_instantiation(self):
        """Test invalid type during initialization.

        Raises:
            AssertionError: If the error message does not match the expected output.
        """

        class SimpleModel(DataModel):
            """A simple model for type validation.

            Attributes:
                count (int): The count value.
            """

            count: int

        with pytest.raises(TypeError) as exc:
            SimpleModel(count="five")

        error = type_error_to_dict(exc)
        assert error == {"count": "Expected type int, got str"}

    def test_optional_fields(self):
        """Test optional fields with default values.

        Raises:
            AssertionError: If the optional fields do not behave as expected.
        """

        class OptionalModel(DataModel):
            """A model with an optional field.

            Attributes:
                name (str): The name.
                age (Optional[int]): The optional age.
            """

            name: str
            age: Optional[int] = None

        obj1 = OptionalModel(name="Alice")
        assert obj1.age is None

        obj2 = OptionalModel(name="Bob", age=30)
        assert obj2.age == 30

        with pytest.raises(TypeError):
            OptionalModel(name="Charlie", age="thirty")

    def test_union_types(self):
        """Test union type validation.

        Raises:
            AssertionError: If the union type validation error message is incorrect.
        """

        class UnionModel(DataModel):
            """A model with a union type attribute.

            Attributes:
                identifier (Union[int, float]): The identifier.
            """

            identifier: Union[int, float]

        UnionModel(identifier=42)
        UnionModel(identifier=42.2)

        with pytest.raises(TypeError) as exc:
            UnionModel(identifier="wrong")
        error = type_error_to_dict(exc)
        assert error == {
            "identifier": "Value did not match any candidate in Union: got str"
        }

    def test_nested_models(self):
        """Test validation of nested DataModel instances.

        Raises:
            AssertionError: If nested models do not validate as expected.
        """
        address = Address(street="123 Main St", zipcode=90210)
        product = Product(id=1, name="Widget", price=9.99)

        obj = ComplexModel(
            id="abc123", metadata={"version": 1.0}, products=[product], address=address
        )

        assert obj.address.zipcode == 90210

        with pytest.raises(TypeError) as exc:
            ComplexModel(
                id=123,
                metadata={},
                products=[{"id": "wrong", "name": "Widget", "price": 9.99}],
            )
        error = type_error_to_dict(exc)
        assert error == {"products.0.id": "Expected type int, got str"}

    def test_list_type_validation(self):
        """Test list type parameter validation.

        Raises:
            AssertionError: If list type validation error message is incorrect.
        """

        class ListModel(DataModel):
            """A model with a list attribute.

            Attributes:
                values (List[int]): A list of integers.
            """

            values: List[int]

        ListModel(values=[1, 2, 3])

        with pytest.raises(TypeError) as exc:
            ListModel(values=[1, "wrong", 3])
        error = type_error_to_dict(exc)
        assert error == {"values.1": "Expected type int, got str"}

    def test_dict_type_validation(self):
        """Test dictionary type validation.

        Raises:
            AssertionError: If dictionary type validation error message is incorrect.
        """

        class DictModel(DataModel):
            """A model with a dictionary attribute.

            Attributes:
                mapping (Dict[str, int]): A mapping of string keys to integer values.
            """

            mapping: Dict[str, int]

        DictModel(mapping={"a": 1, "b": 2})

        with pytest.raises(TypeError) as exc:
            DictModel(mapping={"a": 1, "b": "wrong"})
        error = type_error_to_dict(exc)
        assert error == {"mapping.b": "Expected type int, got str"}

    def test_class_variable_validation(self):
        """Test class variable validation.

        Raises:
            AssertionError: If invalid class variable types are not caught.
        """

        class ValidClassVars(DataModel):
            """A model with valid class variables.

            Class Attributes:
                MAX_SIZE (int): Maximum size.
            Attributes:
                name (str): The name.
            """

            MAX_SIZE: ClassVar[int] = 100
            name: str

        with pytest.raises(TypeError) as exc:

            class InvalidClassVars(DataModel):
                """A model with an invalid class variable type.

                Class Attributes:
                    MAX_SIZE (int): Maximum size.
                Attributes:
                    name (str): The name.
                """

                MAX_SIZE: ClassVar[int] = "100"
                name: str

        assert "Class attribute MAX_SIZE must be <class 'int'>" in str(exc.value)

    def test_deeply_nested_structures(self):
        """Test multi-level nested structures.

        Raises:
            AssertionError: If nested structures do not validate correctly.
        """

        class DeepModel(DataModel):
            """A model with deeply nested structures.

            Attributes:
                matrix (List[List[Union[int, float]]]): A matrix of numbers.
                nested (Dict[str, Dict[int, List[Address]]]): Nested address data.
            """

            matrix: List[List[Union[int, float]]]
            nested: Dict[str, Dict[int, List[Address]]]

        valid_data = {
            "matrix": [[1, 2.0], [3.5, 4]],
            "nested": {
                "cities": {
                    1: [Address(street="Main St", zipcode=12345)],
                    2: [Address(street="Oak St", zipcode="67890")],
                }
            },
        }

        obj = DeepModel(**valid_data)
        assert len(obj.nested["cities"][2][0].street) > 0

    def test_post_init_validation(self):
        """Test validation when setting attributes after initialization.

        Raises:
            AssertionError: If post initialization assignment does not validate.
        """

        class UpdateModel(DataModel):
            """A model to test post initialization attribute update.

            Attributes:
                value (int): An integer value.
            """

            value: int

        obj = UpdateModel(value=42)
        obj.value = 100
        assert obj.value == 100

        with pytest.raises(TypeError):
            obj.value = "invalid"

    def test_missing_required_fields(self):
        """Test missing required fields.

        Raises:
            AssertionError: If missing required field error message is incorrect.
        """

        class RequiredModel(DataModel):
            """A model with required fields.

            Attributes:
                name (str): The name.
                age (int): The age.
            """

            name: str
            age: int

        with pytest.raises(TypeError) as exc:
            RequiredModel(name="Alice")
        error = type_error_to_dict(exc)
        assert error == {"age": "Missing required field"}

    def test_any_type_handling(self):
        """Test Any type validation.

        This test ensures that fields annotated as Any accept any type.
        """

        class AnyModel(DataModel):
            """A model with an Any type attribute.

            Attributes:
                data (Any): Data of any type.
            """

            data: Any

        AnyModel(data=42)
        AnyModel(data="string")
        AnyModel(data={"complex": object()})

    def test_forward_references(self):
        """Test forward references in type hints.

        Raises:
            AssertionError: If forward reference validation fails.
        """

        class Node(DataModel):
            """A node model using forward references.

            Attributes:
                value (int): The node value.
                next (Optional[Node]): The next node.
            """

            value: int
            next: Optional["Node"] = None

        node1 = Node(value=1)
        node2 = Node(value=2, next=node1)
        assert node2.next.value == 1

        with pytest.raises(TypeError):
            Node(value=3, next="invalid")

    def test_large_complex_model(self):
        """Test a large and complex model.

        Raises:
            AssertionError: If the model does not correctly validate large complex data.
        """

        class BigModel(DataModel):
            """A large complex model.

            Attributes:
                id (Union[int, str]): The identifier.
                metadata (Dict[str, Any]): Metadata dictionary.
                items (List[Dict[str, Union[int, float, str]]]): List of item dictionaries.
                nested (Optional[List[List[Dict[str, Address]]]]): Nested address lists.
            Class Attributes:
                counter (int): A counter.
            """

            id: Union[int, str]
            metadata: Dict[str, Any]
            items: List[Dict[str, Union[int, float, str]]]
            nested: Optional[List[List[Dict[str, Address]]]]
            counter: ClassVar[int] = 0

        valid_data = {
            "id": "123e4567-e89b-12d3-a456-426614174000",
            "metadata": {"version": 1.0, "active": True},
            "items": [{"a": 1, "b": 2.5}, {"c": "three", "d": 4}],
            "nested": [
                [{"main": Address(street="123 Main", zipcode=12345)}],
                [{"secondary": Address(street="456 Oak", zipcode="67890")}],
            ],
        }

        obj = BigModel(**valid_data)
        assert len(obj.items) == 2
        assert obj.nested[1][0]["secondary"].zipcode == "67890"

    def test_error_messages(self):
        """Test quality of error messages.

        Raises:
            AssertionError: If error messages do not contain the expected text.
        """

        class ErrorModel(DataModel):
            """A model to test error messages.

            Attributes:
                age (int): The age.
                address (Address): The address.
            """

            age: int
            address: Address

        with pytest.raises(TypeError) as exc:
            ErrorModel(age="thirty", address={"street": 123, "zipcode": 456})
        error_msg = str(exc.value)
        assert "Expected type int, got str" in error_msg

    def test_inheritance_behavior(self):
        """Test model inheritance and annotation merging.

        Raises:
            AssertionError: If inheritance does not merge annotations correctly.
        """

        class ParentModel(DataModel):
            """A parent model.

            Attributes:
                base_field (int): The base field.
                optional_field (str): An optional field with a default value.
            """

            base_field: int
            optional_field: str = "default"

        class ChildModel(ParentModel):
            """A child model inheriting from ParentModel.

            Attributes:
                child_field (float): A child-specific field.
                optional_field (str): Overrides parent's optional_field.
            """

            child_field: float
            optional_field: str

        obj = ChildModel(base_field=42, child_field=3.14, optional_field="custom")
        assert obj.optional_field == "custom"

        with pytest.raises(TypeError):
            ChildModel(child_field=1.618)

    def test_cyclic_structures(self):
        """Test cyclic data structures.

        Raises:
            AssertionError: If cyclic structure handling fails.
        """

        class TreeNode(DataModel):
            """A tree node supporting cyclic structures.

            Attributes:
                value (int): The node value.
                children (List[TreeNode]): List of child nodes.
            """

            value: int
            children: List["TreeNode"] = []

        root = TreeNode(value=0)
        child1 = TreeNode(value=1)
        child2 = TreeNode(value=2)
        root.children = [child1, child2]
        assert len(root.children) == 2
        assert root.children[0].value == 1

    def test_generic_containers(self):
        """Test various generic container types.

        Raises:
            AssertionError: If container type validation does not work as expected.
        """
        from typing import Tuple, Set

        class ContainerModel(DataModel):
            """A model with generic container types.

            Attributes:
                tuple_data (Tuple[int, str]): A tuple of an integer and a string.
                set_data (Set[float]): A set of floats.
            """

            tuple_data: Tuple[int, str]
            set_data: Set[float]

        ContainerModel(tuple_data=(42, "answer"), set_data={3.14, 2.718})

        with pytest.raises(TypeError):
            ContainerModel(tuple_data=("answer", 42), set_data={1, 2, 3})

    def test_validation_performance(self, capsys):
        """Test performance with large datasets.

        Args:
            capsys: Pytest fixture for capturing output.

        Raises:
            AssertionError: If the performance test does not complete as expected.
        """

        class BigDataModel(DataModel):
            """A model for performance testing with large datasets.

            Attributes:
                matrix (List[List[int]]): A 2D matrix of integers.
            """

            matrix: List[List[int]]

        data = [[i * 1000 + j for j in range(1000)] for i in range(1000)]
        with capsys.disabled():
            obj = BigDataModel(matrix=data)
            assert len(obj.matrix) == 1000
            assert obj.matrix[999][999] == 999999

    def test_init_to_float(self):
        """Test initialization conversion to float.

        Raises:
            AssertionError: If the value is not correctly converted to float.
        """

        class Test(DataModel):
            """A model to test float conversion during initialization.

            Attributes:
                value (float): The float value.
            """

            value: float

        obj = Test(value=1)
        assert obj.value == 1.0

    def test_default_values(self):
        """Test default values for fields.

        Raises:
            AssertionError: If default field values are not set correctly.
        """

        class DefaultModel(DataModel):
            """A model with default field values.

            Attributes:
                name (str): The name with a default value.
                age (int): The age with a default value.
            """

            name: str = "default_name"
            age: int = 25

        obj = DefaultModel()
        assert obj.name == "default_name"
        assert obj.age == 25

    def test_field_aliasing(self):
        """Test field aliasing.

        Raises:
            AssertionError: If field aliasing does not resolve to the expected field.
        """

        class AliasModel(DataModel):
            """A model demonstrating single field aliasing.

            Attributes:
                actual_name (str): The actual name, aliased from "name".
            """

            actual_name: str = Field(alias="name")

        obj = AliasModel(name="Alice")
        assert obj.actual_name == "Alice"

        class MultiAliasModel(DataModel):
            """A model demonstrating multiple field aliasing.

            Attributes:
                actual_name (str): The actual name, aliased from "name" or "nickname".
            """

            actual_name: str = Field(alias=["name", "nickname"])

        obj = MultiAliasModel(nickname="Bob")
        assert obj.actual_name == "Bob"
        obj = MultiAliasModel(name="Alice")
        assert obj.actual_name == "Alice"

    def test_model_equality(self):
        """Test model equality.

        Raises:
            AssertionError: If two identical models are not considered equal.
        """

        class EqualityModel(DataModel):
            """A model for testing equality.

            Attributes:
                id (int): The identifier.
                name (str): The name.
            """

            id: int
            name: str

        obj1 = EqualityModel(id=1, name="Test")
        obj2 = EqualityModel(id=1, name="Test")
        assert obj1 == obj2

    def test_model_copy(self):
        """Test model copying via deepcopy.

        Raises:
            AssertionError: If the copied model is not equal but distinct from the original.
        """

        class CopyModel(DataModel):
            """A model to test copying.

            Attributes:
                id (int): The identifier.
                name (str): The name.
            """

            id: int
            name: str

        obj1 = CopyModel(id=1, name="Test")
        obj2 = copy.deepcopy(obj1)
        assert obj1 == obj2
        assert obj1 is not obj2

    def test_model_update(self):
        """Test updating a model's attributes.

        Raises:
            AssertionError: If the model update does not reflect the changes.
        """

        class UpdateModel(DataModel):
            """A model for testing attribute updates.

            Attributes:
                id (int): The identifier.
                name (str): The name.
            """

            id: int
            name: str

        obj = UpdateModel(id=1, name="Test")
        obj.name = "Updated"
        assert obj.name == "Updated"

    def test_model_inheritance_with_additional_fields(self):
        """Test model inheritance with additional fields.

        Raises:
            AssertionError: If inherited fields are not correctly available.
        """

        class ParentModel(DataModel):
            """A parent model.

            Attributes:
                id (int): The identifier.
            """

            id: int

        class ChildModel(ParentModel):
            """A child model that adds additional fields.

            Attributes:
                name (str): The name.
            """

            name: str

        obj = ChildModel(id=1, name="Test")
        assert obj.id == 1
        assert obj.name == "Test"

    def test_model_with_model_as_dict_value(self):
        """Test a model with another model as a dictionary value.

        Raises:
            AssertionError: If the nested model in a dict does not validate as expected.
        """

        class ModelWithDict(DataModel):
            """A model with dictionary values as Product instances.

            Attributes:
                data (Dict[str, Product]): A mapping from string to Product.
            """

            data: Dict[str, Product]

        m = ModelWithDict(
            data={
                "product1": Product(id=1, name="Test", price=10.0),
                "product2": Product(id=2, name="Test2", price=20.0),
            }
        )
        assert m.data["product1"].name == "Test"
        assert m.data["product2"].price == 20.0

        with pytest.raises(TypeError) as exc:
            ModelWithDict(
                data={
                    "product1": {"id": "wrong", "name": "Test", "price": "wrong2"},
                    "product2": Product(id=2, name="Test2", price=20.0),
                }
            )
        error = type_error_to_dict(exc)
        assert error == {
            "data.product1.id": "Expected type int, got str",
            "data.product1.price": "Expected type float, got str",
        }

    def test_model_with_model_as_dict_value_with_union(self):
        """Test a model with dictionary values that can be multiple model types.

        Raises:
            AssertionError: If the union type for dict values does not validate correctly.
        """

        class ModelWithDict(DataModel):
            """A model where dictionary values can be Address, Product, or ProductWithStrId.

            Attributes:
                data (dict[str, Union[Address, Product, ProductWithStrId]]): The data mapping.
            """

            data: dict[str, Union[Address, Product, ProductWithStrId]]

        m = ModelWithDict(
            data={
                "product1": Product(id=1, name="Test", price=10.0),
                "address1": Address(street="123 Main St", zipcode=90210),
            }
        )
        assert m.data["product1"].name == "Test"
        assert m.data["address1"].zipcode == 90210

        m = ModelWithDict(
            data={
                "product1": {"id": "wrong", "name": "Test", "price": 10.0},
                "address1": Address(street="123 Main St", zipcode=90210),
            }
        )
        assert m.data["address1"].zipcode == 90210
        assert m.data["product1"].id == "wrong"
