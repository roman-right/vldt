from typing import Union, ClassVar, Dict, Any, List, Optional

from vldt import DataModel


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