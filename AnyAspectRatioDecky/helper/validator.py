import string
from urllib.parse import urlparse


# Plugin general string checking rules.
class Charset:
    DIGITS = frozenset(string.digits)
    LOWER = frozenset(string.ascii_lowercase)
    LETTERS = frozenset(string.ascii_letters)
    ALNUM = LETTERS | DIGITS
    LOWER_ALNUM = LOWER | DIGITS
    HEX = DIGITS | frozenset("abcdef")

    # True when value is a non-empty string made only of `allowed` characters.
    @staticmethod
    def matches(value: object, allowed: frozenset[str]) -> bool:
        return isinstance(value, str) and bool(value) and allowed.issuperset(value)


class Validator:
    class Paths:
        _UNSAFE_CHARACTERS = frozenset("\\:\0")
        _NAVIGATION_PARTS = frozenset(("", ".", ".."))

        @classmethod
        def is_relative(cls, path: str) -> bool:
            return (
                bool(path)
                and cls._UNSAFE_CHARACTERS.isdisjoint(path)
                and cls._NAVIGATION_PARTS.isdisjoint(path.split("/"))
            )

    class Files:
        """File and folder names met while scanning folders and zips."""

        # Added to zips by macOS and Windows themselves; never part of a mod.
        _OS_JUNK_FOLDER = "__MACOSX/"
        _OS_JUNK_FILES = frozenset((".ds_store", "thumbs.db"))
        # Name fragments of installers, crash reporters, launchers and anti-cheat, never a game's main .exe.
        _HELPER_EXECUTABLE_PARTS = (
            "unins", "crash", "redist", "vcredist", "setup", "installer", "launcher", "eac", "easyanticheat", "dxsetup",
        )

        @staticmethod
        def has_extension(name: str, extension: str) -> bool:
            return name.lower().endswith(extension)

        @staticmethod
        def is_hidden(name: str) -> bool:
            return name.startswith(".")

        @classmethod
        def is_os_junk(cls, name: str) -> bool:
            """name is a "/"-separated path inside a zip."""
            return name.startswith(cls._OS_JUNK_FOLDER) or name.rsplit("/", 1)[-1].lower() in cls._OS_JUNK_FILES

        @classmethod
        def is_helper_executable(cls, name: str) -> bool:
            name = name.lower()
            return any(part in name for part in cls._HELPER_EXECUTABLE_PARTS)

    class Urls:
        @staticmethod
        def is_https(value: str) -> bool:
            return urlparse(value).scheme == "https"

    class GitHub:
        _NAME = Charset.ALNUM | frozenset("._-")
        _REF_FORBIDDEN = frozenset(" ~^:?*[\\")

        @classmethod
        def repository(cls, url: str) -> bool:
            """https://github.com/<owner>/<repo>"""

            parsed = urlparse(url)
            segments = parsed.path.strip("/").split("/")
            return (
                parsed.scheme == "https"
                and parsed.hostname == "github.com"
                and not parsed.query
                and not parsed.fragment
                and len(segments) == 2
                and all(Charset.matches(segment, cls._NAME) for segment in segments)
            )

        @classmethod
        def ref(cls, ref: str) -> bool:
            return (
                bool(ref)
                and ref.isprintable()
                and cls._REF_FORBIDDEN.isdisjoint(ref)
                and ".." not in ref
                and "@{" not in ref
                and not ref.startswith(("/", "-"))
                and not ref.endswith(("/", ".", ".lock"))
            )

    class Catalog:
        _MOD_ID = Charset.LOWER_ALNUM | frozenset("-")
        _DISPLAY_TAG = Charset.LOWER_ALNUM | frozenset("_-")

        @classmethod
        def mod_id(cls, value: str) -> bool:
            return Charset.matches(value, cls._MOD_ID)

        @classmethod
        def display_tag(cls, value: object) -> bool:
            return Charset.matches(value, cls._DISPLAY_TAG)

        @staticmethod
        def steam_app_id(value: str) -> bool:
            return value.isascii() and value.isdigit()

    class Wine:
        """WINEDLLOVERRIDES entries, for example dinput8=n,b."""

        # No spaces, quotes or ";", so an entry can never break out of the Steam launch option.
        _DLL = Charset.LOWER_ALNUM | frozenset("_-")
        # Wine's load orders: native then builtin.
        _MODES = frozenset(("n", "b"))
        # DLLs that Wine only loads from the game directory when overridden to "native".
        _PROXY_DLLS = frozenset((
            "d3d9", "d3d11", "dinput8", "dsound", "dxgi", "version", "wininet", "winhttp", "winmm",
            "xinput1_3", "xinput1_4", "xinput9_1_0",
        ))

        @classmethod
        def override(cls, value: object) -> bool:
            """dll[,dll...]=mode[,mode]"""
            if not isinstance(value, str):
                return False
            dlls, separator, modes = value.partition("=")
            return (
                bool(separator)
                and all(Charset.matches(dll, cls._DLL) for dll in dlls.split(","))
                and cls._MODES.issuperset(modes.split(","))
            )

        @classmethod
        def is_proxy_dll(cls, name: str) -> bool:
            """name is the DLL's file name without ".dll", in any case."""
            return name.lower() in cls._PROXY_DLLS

    class Ini:
        _KEY = Charset.ALNUM | frozenset("_")

        @staticmethod
        def section(value: str) -> bool:
            """The text between "[" and "]" on one line, example: [Resolution]"""
            return bool(value) and value.isascii() and value.isprintable() and "]" not in value and value == value.strip()

        @classmethod
        def key(cls, value: str) -> bool:
            return Charset.matches(value, cls._KEY) and value[0].isalpha()

    class Hash:
        @staticmethod
        def sha256(value: object) -> bool:
            return isinstance(value, str) and len(value) == 64 and Charset.HEX.issuperset(value)

    class Settings:
        # Bounded
        MAX_RESOLUTION = 16384

        @classmethod
        def resolution(cls, value: object) -> bool:
            return type(value) is int and 1 <= value <= cls.MAX_RESOLUTION
