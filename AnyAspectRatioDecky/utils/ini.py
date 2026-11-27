"""Read INI lines in place: [section] headers and key = value lines, with each value's position.

Values are replaced by position, so everything else in the file (comments, spacing, line endings,
a UTF-8 BOM) is kept exactly as the user wrote it.
"""

from dataclasses import dataclass

from helper.validator import Validator

_BOM = b"\xef\xbb\xbf"
_BLANKS = b" \t"
_QUOTES = (b'"', b"'")


@dataclass(frozen=True, slots=True)
class Value:
    """Where one value sits: lines[line][start:end]."""

    line: int
    start: int
    end: int

    def read(self, lines: list[bytes]) -> bytes:
        return lines[self.line][self.start:self.end]


def values_by_section(lines: list[bytes]) -> dict[bytes, dict[bytes, list[Value]]]:
    """Every key = value line, grouped by lower-case section name, then lower-case key."""
    sections: dict[bytes, dict[bytes, list[Value]]] = {}
    section = b""
    for index, line in enumerate(lines):
        offset = len(_BOM) if index == 0 and line.startswith(_BOM) else 0
        body = line[offset:].rstrip(b"\r\n")
        if (name := _section_name(body)) is not None:
            section = name
            continue
        if (assignment := _assignment(body)) is not None:
            key, start, end = assignment
            sections.setdefault(section, {}).setdefault(key, []).append(Value(index, start + offset, end + offset))
    return sections


def _section_name(body: bytes) -> bytes | None:
    """The lower-case name of a "[Section]" line."""
    stripped = body.strip()
    if not stripped.startswith(b"[") or b"]" not in stripped:
        return None
    return stripped[1:stripped.index(b"]")].strip().lower()


def _assignment(body: bytes) -> tuple[bytes, int, int] | None:
    """(lower-case key, value start, value end) of a "key = value ; comment" line."""
    key_text, equals, _ = body.partition(b"=")
    key = key_text.strip(_BLANKS)
    # latin-1 maps each byte to one character, so any non-ASCII byte fails the key rule.
    if not equals or not Validator.Ini.key(key.decode("latin-1")):
        return None
    start = len(key_text) + 1
    start += len(body[start:]) - len(body[start:].lstrip(_BLANKS))
    comment = min((at for at in (body.find(b";", start), body.find(b"#", start)) if at >= 0), default=len(body))
    end = start + len(body[start:comment].rstrip(_BLANKS))
    return key.lower(), start, end


def is_decimal(value: bytes) -> bool:
    """1280 or 1280.0: digits, optionally with one fraction part."""
    whole, dot, fraction = value.partition(b".")
    return whole.isdigit() and (not dot or fraction.isdigit())


def _quote(value: bytes) -> bytes:
    return value[:1] if len(value) >= 2 and value[:1] in _QUOTES and value.endswith(value[:1]) else b""


def auto_quote(value: bytes) -> bytes | None:
    """For an "auto" value, the quote around it; None for anything else."""
    quote = _quote(value)
    return quote if value[len(quote):len(value) - len(quote)].lower() == b"auto" else None


def resolution_quote(value: bytes) -> bytes | None:
    """For a "WIDTHxHEIGHT" value, the quote around it (b'"', b"'" or b""); None for anything else."""
    quote = _quote(value)
    width, x, height = value[len(quote):len(value) - len(quote)].partition(b"x")
    if x and all(part.isdigit() and not part.startswith(b"0") for part in (width, height)):
        return quote
    return None
