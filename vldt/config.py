from vldt.deserializer import GLOBAL_DESERIALIZER

_VALID_EXTRA = ("ignore", "allow", "forbid")


class Config:
    """Configuration class for vldt models.

    Attributes:
        dict_serializer (dict): Encoder for dictionaries.
        json_serializer (dict): Encoder for JSON.
        deserializer (dict): Deserializer.
        extra (str): How to handle keys not declared on the model.
            "ignore" (default): silently drop unknown keys.
            "allow": store them on the instance and serialize them out.
            "forbid": raise a TypeError mentioning the unknown keys.
    """

    def __init__(
        self,
        dict_serializer=None,
        json_serializer=None,
        deserializer=None,
        extra="ignore",
    ):
        """Initialize the Config instance."""
        self.dict_serializer = dict_serializer if dict_serializer is not None else {}
        self.json_serializer = json_serializer if json_serializer is not None else {}
        deserializer = deserializer if deserializer is not None else {}
        self.deserializer = GLOBAL_DESERIALIZER | deserializer
        if extra not in _VALID_EXTRA:
            raise ValueError(
                f"Config(extra=...) must be one of {_VALID_EXTRA!r}, got {extra!r}"
            )
        self.extra = extra
