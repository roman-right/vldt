import pytest

from tests.conftest import type_error_to_dict
from tests.models import Product


class TestSetAttr:
    def test_simple(self):
        p = Product(id=1, name="Widget", price=9.99)

        with pytest.raises(TypeError) as exc:
            p.id = "wrong"

        error = type_error_to_dict(exc)
        assert error == {"id": "Expected type int, got str"}

        p.id = 2
        assert p.id == 2
