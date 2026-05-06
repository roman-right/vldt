class _Undefined:
    """Sentinel class for representing the absence of a default value.

    A bare ``Field()`` carries this sentinel as its default so that the C
    layer can tell "no default provided" from "default is explicitly None".
    """

    _instance = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance

    def __repr__(self):
        return "UNDEFINED"

    def __bool__(self):
        return False


UNDEFINED = _Undefined()


class Field:
    """A class representing a model field with default values and aliases.

    Attributes:
        default (Any): The default value of the field, or UNDEFINED if none.
        default_factory (Callable, optional): A callable to generate the default value.
        alias (list): A list of alternative names for the field.
    """

    def __init__(self, *, default=UNDEFINED, alias=None, default_factory=None):
        """Initialize a Field instance.

        Args:
            default (Any, optional): The default value. Defaults to UNDEFINED
                which means no default was provided and the field is required.
            alias (Union[list, tuple, Any], optional): An alias or a list/tuple of aliases
                for the field. If not provided, defaults to an empty list.
            default_factory (Callable, optional): A callable that returns a default value.
                Cannot be used together with `default`.

        Raises:
            ValueError: If both `default` and `default_factory` are provided.
        """
        if default is not UNDEFINED and default_factory is not None:
            raise ValueError("Cannot specify both default and default_factory")
        self.default = default
        self.default_factory = default_factory
        if alias is None:
            self.alias = []
        elif isinstance(alias, (list, tuple)):
            self.alias = list(alias)
        else:
            self.alias = [alias]

    def get_default(self):
        """Get the default value for the field.

        Returns:
            Any: The default value, obtained from `default_factory` if provided,
            otherwise the `default` value, or UNDEFINED if neither is set.
        """
        if self.default_factory is not None:
            return self.default_factory()
        return self.default
