"""Tests for critical bug fixes.

Each test corresponds to a numbered issue in the GitHub repo and serves as a
regression guard against the bug being reintroduced.
"""

import gc
import sys
from typing import ClassVar, Optional

import pytest

from vldt import DataModel, Field


class TestIssue1EmptyTupleRefcount:
    """Issue #1: empty_tuple global was decremented on every from_json call.

    The bug is masked on CPython 3.12+ because the empty tuple is an immortal
    singleton, but the code is still semantically wrong. A stress test
    documents the expected contract: from_json must remain stable under load.
    """

    def test_from_json_stress(self):
        """Many from_json calls must not crash or corrupt state."""

        class M(DataModel):
            x: int

        for i in range(10000):
            obj = M.from_json('{"x": 42}')
            assert obj.x == 42

    def test_from_json_does_not_leak_empty_tuple_ref(self):
        """The global empty_tuple is shared with Python's empty-tuple singleton.

        Calling from_json many times must not decrement its refcount in an
        observable way. We can't access the C global directly, but we can
        verify the singleton refcount is well-behaved.
        """

        class M(DataModel):
            x: int

        empty = ()
        gc.collect()
        before = sys.getrefcount(empty)
        for _ in range(1000):
            M.from_json('{"x": 1}')
        gc.collect()
        after = sys.getrefcount(empty)

        assert after >= before - 100, (
            f"empty tuple refcount dropped from {before} to {after} "
            "(suggests incorrect Py_DECREF on global)"
        )
