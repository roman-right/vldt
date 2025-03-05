import inspect
from enum import Enum


class ValidatorMode(Enum):
    BEFORE = "before"
    AFTER = "after"


def field_validator(*, mode: ValidatorMode):
    """Decorator for field validators.

    The decorated function should be a classmethod (or a plain function) that takes two
    parameters (the first being the class or instance and the second the value of the field).
    The field name is inferred from the function’s signature (the name of the second parameter).

    Args:
        mode (ValidatorMode): The validator mode.

    Returns:
        Callable: The decorator function with attached metadata.
    """

    def decorator(fn):
        # Get the underlying function if wrapped as a classmethod/staticmethod.
        actual_func = fn.__func__ if isinstance(fn, (classmethod, staticmethod)) else fn
        sig = inspect.signature(actual_func)
        params = list(sig.parameters.keys())
        # Expect exactly one parameter for the field value aside from the first parameter.
        if len(params) != 2:
            raise ValueError(
                "Field validator must have exactly one field parameter (aside from 'cls' or 'self')"
            )
        field_name = params[1]
        meta = {"mode": mode, "field": field_name}
        setattr(actual_func, "__vldt_field_validator__", meta)
        setattr(fn, "__vldt_field_validator__", meta)
        return fn

    return decorator


def model_validator(*, mode: ValidatorMode):
    """Decorator for model validators.

    For BEFORE model validators (typically as a classmethod) the function should accept two
    parameters: (cls, data) where data is the input dictionary. For AFTER model validators,
    the function can be either a classmethod (accepting (cls, instance)) or an instance method
    (accepting only self). In the AFTER model validator, the function is executed and is expected
    to modify the instance (self) directly.

    Args:
        mode (ValidatorMode): The validator mode.

    Returns:
        Callable: The decorator function with attached metadata.
    """

    def decorator(fn):
        actual_func = fn.__func__ if isinstance(fn, (classmethod, staticmethod)) else fn
        sig = inspect.signature(actual_func)
        params = list(sig.parameters.keys())
        if isinstance(fn, (classmethod, staticmethod)):
            if len(params) != 2:
                raise ValueError(
                    "Model validator (as a classmethod) must have exactly one parameter aside from 'cls'"
                )
        else:
            if len(params) != 1:
                raise ValueError(
                    "Model validator (as an instance method) must have no parameter aside from 'self'"
                )
        meta = {"mode": mode}
        setattr(actual_func, "__vldt_model_validator__", meta)
        setattr(fn, "__vldt_model_validator__", meta)
        return fn

    return decorator


def async_field_validator(*, mode: ValidatorMode):
    """Decorator for asynchronous field validators.

    The decorated function should be an async function (or an async classmethod) that accepts
    two parameters (the first being the class or instance, and the second the field value).
    The field name is inferred from the function’s signature.

    Args:
        mode (ValidatorMode): The validator mode.

    Returns:
        Callable: The decorator function with attached metadata.
    """

    def decorator(fn):
        actual_func = fn.__func__ if isinstance(fn, (classmethod, staticmethod)) else fn
        sig = inspect.signature(actual_func)
        params = list(sig.parameters.keys())
        if len(params) != 2:
            raise ValueError(
                "Async field validator must have exactly one field parameter (aside from 'cls' or 'self')"
            )
        field_name = params[1]
        meta = {"mode": mode, "field": field_name, "async": True}
        setattr(actual_func, "__vldt_async_field_validator__", meta)
        setattr(fn, "__vldt_async_field_validator__", meta)
        return fn

    return decorator


def async_model_validator(*, mode: ValidatorMode):
    """Decorator for asynchronous model validators.

    For BEFORE async model validators, the function should accept (cls, data) as parameters.
    For AFTER async model validators, the function can be either a classmethod (accepting (cls,
    instance)) or an instance method (accepting only self). In the AFTER case, the validator is
    expected to modify the instance in place.

    Args:
        mode (ValidatorMode): The validator mode.

    Returns:
        Callable: The decorator function with attached metadata.
    """

    def decorator(fn):
        actual_func = fn.__func__ if isinstance(fn, (classmethod, staticmethod)) else fn
        sig = inspect.signature(actual_func)
        params = list(sig.parameters.keys())
        if isinstance(fn, (classmethod, staticmethod)):
            if len(params) != 2:
                raise ValueError(
                    "Async model validator (as a classmethod) must have exactly one parameter aside from 'cls'"
                )
        else:
            if len(params) != 1:
                raise ValueError(
                    "Async model validator (as an instance method) must have no parameter aside from 'self'"
                )
        meta = {"mode": mode, "async": True}
        setattr(actual_func, "__vldt_async_model_validator__", meta)
        setattr(fn, "__vldt_async_model_validator__", meta)
        return fn

    return decorator
