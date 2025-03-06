import inspect
import sys
from typing import ClassVar, get_type_hints, get_origin, get_args

from vldt._vldt import DataModel as _DataModel
from vldt.config import Config
from vldt.validators import ValidatorMode


class DataModelMeta(type):
    def __new__(mcls, name, bases, namespace):
        cls = super().__new__(mcls, name, bases, namespace)
        return cls

    def __init__(cls, name, bases, namespace):
        """Initialize the class by resolving type annotations and collecting validators.

        This method resolves forward references in type annotations,
        separates class and instance annotations, validates required
        class attributes, and collects field and model validators.

        Args:
            name (str): The name of the class.
            bases (tuple): Base classes.
            namespace (dict): The class namespace.
        """
        globalns = sys.modules[cls.__module__].__dict__
        localns = dict(cls.__dict__)
        localns[cls.__name__] = cls
        try:
            resolved = get_type_hints(
                cls, globalns=globalns, localns=localns, include_extras=True
            )
        except Exception:
            resolved = cls.__annotations__
        cls.__annotations__ = resolved

        class_annotations = {}
        instance_annotations = {}
        for attr_name, attr_type in resolved.items():
            if get_origin(attr_type) is ClassVar:
                class_annotations[attr_name] = get_args(attr_type)[0]
            else:
                instance_annotations[attr_name] = attr_type
        cls.__vldt_class_annotations__ = class_annotations or {}
        cls.__vldt_instance_annotations__ = instance_annotations or {}

        for attr_name, ann_type in class_annotations.items():
            value = getattr(cls, attr_name, None)
            if value is None:
                raise TypeError(f"Missing required class attribute: {attr_name}")
            if not isinstance(value, ann_type):
                raise TypeError(
                    f"Class attribute {attr_name} must be {ann_type}, got {type(value)}"
                )

        field_validators_before = {}
        field_validators_after = {}
        model_validators_before = []
        model_validators_after = []
        for attr_name, attr_value in cls.__dict__.items():
            candidate_funcs = []
            if isinstance(attr_value, (classmethod, staticmethod)):
                candidate_funcs.append(attr_value.__func__)
            else:
                candidate_funcs.append(attr_value)
            for func in candidate_funcs:
                if hasattr(func, "__vldt_field_validator__"):
                    info = getattr(func, "__vldt_field_validator__")
                    mode = info["mode"]
                    field = info["field"]
                    if mode == ValidatorMode.BEFORE:
                        field_validators_before.setdefault(field, []).append(attr_value)
                    elif mode == ValidatorMode.AFTER:
                        field_validators_after.setdefault(field, []).append(attr_value)
                if hasattr(func, "__vldt_model_validator__"):
                    info = getattr(func, "__vldt_model_validator__")
                    mode = info["mode"]
                    if mode == ValidatorMode.BEFORE:
                        model_validators_before.append(attr_value)
                    elif mode == ValidatorMode.AFTER:
                        model_validators_after.append(attr_value)
        cls.__vldt_validators__ = {
            "field_before": field_validators_before,
            "field_after": field_validators_after,
            "model_before": model_validators_before,
            "model_after": model_validators_after,
        }
        cls.__vldt_has_field_before_validators__ = bool(field_validators_before)
        cls.__vldt_has_field_after_validators__ = bool(field_validators_after)
        cls.__vldt_has_model_before_validators__ = bool(model_validators_before)
        cls.__vldt_has_model_after_validators__ = bool(model_validators_after)
        super().__init__(name, bases, namespace)


class DataModel(_DataModel, metaclass=DataModelMeta):
    __vldt_config__: ClassVar[Config] = Config()

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        cls.__vldt_class_annotations__ = getattr(cls, "__vldt_class_annotations__", {})
        cls.__vldt_instance_annotations__ = getattr(
            cls, "__vldt_instance_annotations__", {}
        )

    def __eq__(self, other):
        if hasattr(self, "to_dict") and hasattr(other, "to_dict"):
            return self.to_dict() == other.to_dict()
        return False


class AsyncDataModelMeta(DataModelMeta):
    def __init__(cls, name, bases, namespace):
        """Initialize the async model meta by collecting asynchronous validators.

        This method gathers asynchronous field and model validators
        from the class definition and stores them for later use.

        Args:
            name (str): The name of the class.
            bases (tuple): Base classes.
            namespace (dict): The class namespace.
        """
        super().__init__(name, bases, namespace)
        async_field_before = {}
        async_field_after = {}
        async_model_before = []
        async_model_after = []
        for attr_name, attr_value in cls.__dict__.items():
            candidate_funcs = []
            if isinstance(attr_value, (classmethod, staticmethod)):
                candidate_funcs.append(attr_value.__func__)
            else:
                candidate_funcs.append(attr_value)
            for func in candidate_funcs:
                if hasattr(func, "__vldt_async_field_validator__"):
                    info = getattr(func, "__vldt_async_field_validator__")
                    mode = info["mode"]
                    field = info["field"]
                    if mode == ValidatorMode.BEFORE:
                        async_field_before.setdefault(field, []).append(attr_value)
                    elif mode == ValidatorMode.AFTER:
                        async_field_after.setdefault(field, []).append(attr_value)
                if hasattr(func, "__vldt_async_model_validator__"):
                    info = getattr(func, "__vldt_async_model_validator__")
                    mode = info["mode"]
                    if mode == ValidatorMode.BEFORE:
                        async_model_before.append(attr_value)
                    elif mode == ValidatorMode.AFTER:
                        async_model_after.append(attr_value)
        cls.__async_validators__ = {
            "field_before": async_field_before,
            "field_after": async_field_after,
            "model_before": async_model_before,
            "model_after": async_model_after,
        }
        cls.__vldt_has_async_field_before_validators__ = bool(async_field_before)
        cls.__vldt_has_async_field_after_validators__ = bool(async_field_after)
        cls.__vldt_has_async_model_before_validators__ = bool(async_model_before)
        cls.__vldt_has_async_model_after_validators__ = bool(async_model_after)


class AsyncDataModel(DataModel, metaclass=AsyncDataModelMeta):
    """AsyncDataModel supports both synchronous (via the underlying C++ DataModel)
    and asynchronous validators. Its __init__ simply stores the keyword arguments,
    and the full initialization (including running async BEFORE validators, then the
    synchronous C++ initialization, then async AFTER validators) occurs when the
    instance is awaited.

    Usage:
        person = await AsyncDataModel(name="john", age="20")
    """

    def __new__(cls, *args, **kwargs):
        # Allocate an instance without calling the C++ initialization (tp_init).
        return DataModel.__new__(cls)

    def __init__(self, *args, **kwargs):
        # Instead of running validations immediately, store the kwargs.
        self._init_kwargs = kwargs.copy()

    def _sync_init(self, kwargs):
        """Call the synchronous DataModel initialization code on this instance.

        Args:
            kwargs (dict): The keyword arguments for initialization.
        """
        DataModel.__init__(self, **kwargs)

    async def _run_async_before(self, kwargs: dict) -> dict:
        """Run asynchronous BEFORE validators on the input kwargs.

        This method awaits each async model and field validator and updates
        the kwargs accordingly.

        Args:
            kwargs (dict): The input keyword arguments.

        Returns:
            dict: The updated keyword arguments.
        """
        if getattr(self.__class__, "__vldt_has_async_model_before_validators__", False):
            for validator in self.__class__.__async_validators__.get(
                "model_before", []
            ):
                if isinstance(validator, (classmethod, staticmethod)):
                    result = await validator.__func__(self.__class__, kwargs)
                else:
                    result = await validator(kwargs)
                if isinstance(result, dict):
                    kwargs.update(result)
        if getattr(self.__class__, "__vldt_has_async_field_before_validators__", False):
            for field, validators in self.__class__.__async_validators__.get(
                "field_before", {}
            ).items():
                if field in kwargs:
                    value = kwargs[field]
                    for validator in validators:
                        if isinstance(validator, (classmethod, staticmethod)):
                            value = await validator.__func__(self.__class__, value)
                        else:
                            value = await validator(value)
                    kwargs[field] = value
        return kwargs

    async def _run_async_after(self):
        """Run asynchronous AFTER validators on this instance.

        This method awaits async field and model validators that modify the instance.
        """
        if getattr(self.__class__, "__vldt_has_async_field_after_validators__", False):
            for field, validators in self.__class__.__async_validators__.get(
                "field_after", {}
            ).items():
                if hasattr(self, field):
                    value = getattr(self, field)
                    for validator in validators:
                        if isinstance(validator, (classmethod, staticmethod)):
                            value = await validator.__func__(self.__class__, value)
                        else:
                            value = await validator(value)
                    setattr(self, field, value)
        if getattr(self.__class__, "__vldt_has_async_model_after_validators__", False):
            for validator in self.__class__.__async_validators__.get("model_after", []):
                if inspect.iscoroutinefunction(validator):
                    await validator(self)
                else:
                    await validator.__func__(self.__class__, self)

    async def _async_init(self):
        """Perform asynchronous initialization.

        This routine:
          1. Runs async BEFORE validators on the stored kwargs.
          2. Calls the synchronous C++ initialization.
          3. Runs async AFTER validators.
          4. Returns the fully initialized instance.

        Returns:
            AsyncDataModel: The initialized instance.
        """
        updated_kwargs = await self._run_async_before(self._init_kwargs)
        self._init_kwargs = updated_kwargs
        self._sync_init(updated_kwargs)
        await self._run_async_after()
        return self

    def __await__(self):
        return self._async_init().__await__()
