from collections.abc import Callable
from typing import TypeVar

T = TypeVar("T")


def retry_once(action: Callable[[], T], retryable: Callable[[Exception], bool], recover: Callable[[], object]) -> T:
    try:
        return action()
    except Exception as error:
        if not retryable(error):
            raise
    recover()
    return action()
