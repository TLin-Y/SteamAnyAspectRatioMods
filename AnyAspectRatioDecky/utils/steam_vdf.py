import json
from pathlib import Path

from helper.constants import (
    STEAM_VDF_ENCODING,
    VDF_INVALID_ESCAPE_MESSAGE,
    VDF_MISSING_OBJECT_KEY_MESSAGE,
    VDF_MISSING_VALUE_MESSAGE,
    VDF_TOKEN_PATTERN,
    VDF_TRAILING_DATA_MESSAGE,
    VDF_UNCLOSED_OBJECT_MESSAGE,
    VDF_UNEXPECTED_CHARACTER_MESSAGE,
    VDF_UNEXPECTED_CLOSING_BRACE_MESSAGE,
)


class VdfParseError(ValueError):
    pass


def parse_vdf(text: str) -> dict[str, object]:
    tokens: list[str] = []
    position = 0
    while position < len(text):
        match = VDF_TOKEN_PATTERN.match(text, position)
        if match is None:
            if text[position].isspace():
                position += 1
                continue
            raise VdfParseError(VDF_UNEXPECTED_CHARACTER_MESSAGE.format(offset=position))
        position = match.end()
        quoted, brace, bare = match.groups()
        if quoted is not None:
            try:
                tokens.append(json.loads(f'"{quoted}"'))
            except json.JSONDecodeError as error:
                raise VdfParseError(VDF_INVALID_ESCAPE_MESSAGE) from error
        elif brace is not None:
            tokens.append(brace)
        elif bare is not None:
            tokens.append(bare)

    def parse_object(index: int, nested: bool) -> tuple[dict[str, object], int]:
        result: dict[str, object] = {}
        while index < len(tokens):
            key = tokens[index]
            if key == "}":
                if not nested:
                    raise VdfParseError(VDF_UNEXPECTED_CLOSING_BRACE_MESSAGE)
                return result, index + 1
            if key == "{":
                raise VdfParseError(VDF_MISSING_OBJECT_KEY_MESSAGE)
            index += 1
            if index >= len(tokens):
                raise VdfParseError(VDF_MISSING_VALUE_MESSAGE)
            value = tokens[index]
            if value == "{":
                parsed, index = parse_object(index + 1, True)
                result[key] = parsed
            elif value == "}":
                raise VdfParseError(VDF_MISSING_VALUE_MESSAGE)
            else:
                result[key] = value
                index += 1
        if nested:
            raise VdfParseError(VDF_UNCLOSED_OBJECT_MESSAGE)
        return result, index

    parsed, end = parse_object(0, False)
    if end != len(tokens):
        raise VdfParseError(VDF_TRAILING_DATA_MESSAGE)
    return parsed


def read_vdf(path: Path) -> dict[str, object]:
    return parse_vdf(path.read_text(encoding=STEAM_VDF_ENCODING))
