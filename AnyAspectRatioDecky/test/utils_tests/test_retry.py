import unittest
from unittest.mock import Mock

from utils.retry import retry_once


class RetryTests(unittest.TestCase):
    def test_success_needs_no_recovery(self) -> None:
        recover = Mock()
        self.assertEqual("done", retry_once(lambda: "done", lambda _: True, recover))
        recover.assert_not_called()

    def test_retryable_failure_recovers_and_tries_exactly_once_more(self) -> None:
        action = Mock(side_effect=[OSError("gone"), "done"])
        recover = Mock()
        self.assertEqual("done", retry_once(action, lambda error: isinstance(error, OSError), recover))
        recover.assert_called_once_with()
        self.assertEqual(2, action.call_count)

    def test_other_failures_and_a_second_failure_are_raised(self) -> None:
        recover = Mock()
        with self.assertRaisesRegex(ValueError, "bug"):
            retry_once(Mock(side_effect=ValueError("bug")), lambda error: isinstance(error, OSError), recover)
        recover.assert_not_called()
        with self.assertRaisesRegex(OSError, "still gone"):
            retry_once(Mock(side_effect=[OSError("gone"), OSError("still gone")]), lambda _: True, recover)
        recover.assert_called_once_with()
