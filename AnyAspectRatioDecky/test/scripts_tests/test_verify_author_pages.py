import contextlib
import io
import unittest

import verify_author_pages as pages

MOD_PAGE = b'<head><meta property="og:title" content="Keyblade Masters Ultrawide"></head>'
GAME_PAGE = b"<head><title>Kingdom Hearts III Nexus</title></head>"


def replies(*answers):
    calls = []

    def fetcher(url):
        calls.append(url)
        answer = answers[min(len(calls), len(answers)) - 1]
        if isinstance(answer, Exception):
            raise answer
        return answer

    return fetcher, calls


class AuthorPageTests(unittest.TestCase):
    def check(self, *answers):
        fetcher, calls = replies(*answers)
        return pages.check_page("https://example.com/mod", fetcher, sleep=lambda _: None), len(calls)

    def test_a_page_naming_the_mod_passes(self) -> None:
        self.assertEqual((("ok", "Keyblade Masters Ultrawide"), 1), self.check((200, MOD_PAGE)))

    def test_a_deleted_page_fails_at_once(self) -> None:
        self.assertEqual(pages.MISSING, self.check((404, b""))[0][0])
        (result, _), calls = self.check((200, GAME_PAGE))
        self.assertEqual((pages.MISSING, 1), (result, calls))

    def test_refusals_and_failures_are_retried_before_giving_up(self) -> None:
        self.assertEqual((("ok", "Keyblade Masters Ultrawide"), 2), self.check((403, b""), (200, MOD_PAGE)))
        (result, _), calls = self.check((403, b""))
        self.assertEqual((pages.BLOCKED, pages.ATTEMPTS), (result, calls))
        (result, _), calls = self.check(OSError("offline"))
        self.assertEqual((pages.UNREACHABLE, pages.ATTEMPTS), (result, calls))

    def test_the_exit_code_reports_the_worst_page(self) -> None:
        def run(*answers):
            fetcher, _ = replies(*answers)
            with contextlib.redirect_stdout(io.StringIO()):
                return pages.main(fetcher, sleep=lambda _: None)

        self.assertEqual(0, run((200, MOD_PAGE)))
        self.assertEqual(3, run((403, b"")))
        self.assertEqual(2, run(OSError("offline")))
        self.assertEqual(1, run((404, b"")))
