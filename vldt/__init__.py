import importlib.metadata

from vldt.config import Config
from vldt.fields import Field
from vldt.models import DataModel, AsyncDataModel
from vldt.validators import (
    ValidatorMode,
    field_validator,
    model_validator,
    async_field_validator,
    async_model_validator,
)

try:
    __version__ = importlib.metadata.version("vldt")
except importlib.metadata.PackageNotFoundError:
    __version__ = "0.0.0"

__all__ = [
    "AsyncDataModel",
    "DataModel",
    "ValidatorMode",
    "async_field_validator",
    "async_model_validator",
    "field_validator",
    "model_validator",
    "Field",
    "Config",
    "__version__",
]
