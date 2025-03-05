from vldt.deserializer import GLOBAL_DESERIALIZER


class Config:
    """Configuration class for vldt models.

    Attributes:
        dict_serializer (dict): Encoder for dictionaries.
        json_serializer (dict): Encoder for JSON.
        deserializer (dict): Deserializer.
    """

    def __init__(self, dict_serializer=None, json_serializer=None, deserializer=None):
        """Initialize the Config instance.

        Args:
            dict_serializer (dict, optional): Encoder for dictionaries. Defaults to {}.
            json_serializer (dict, optional): Encoder for JSON. Defaults to {}.
            deserializer (dict, optional): Deserializer.
        """
        self.dict_serializer = dict_serializer if dict_serializer is not None else {}
        self.json_serializer = json_serializer if json_serializer is not None else {}
        deserializer = deserializer if deserializer is not None else {}
        self.deserializer = GLOBAL_DESERIALIZER | deserializer
