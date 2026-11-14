import json
import re
from pathlib import Path

_TOKEN_PATTERN = re.compile(r'"((?:\\.|[^"\\])*)"|(\{|\})|//[^\r\n]*|([^\s{}"]+)')



class VdfParseError(ValueError):
    pass


def parse_vdf(text: str) -> dict[str, object]:
    tokens: list[str] = []
    position = 0
    while position < len(text):
        match = _TOKEN_PATTERN.match(text, position)
        if match is None:
            if text[position].isspace():
                position += 1
                continue
            raise VdfParseError(f"Unexpected character at offset {position}")
        position = match.end()
        quoted, brace, bare = match.groups()
        if quoted is not None:
            try:
                tokens.append(json.loads(f'"{quoted}"'))
            except json.JSONDecodeError as error:
                raise VdfParseError("Invalid escape in quoted VDF value") from error
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
                    raise VdfParseError("Unexpected closing brace")
                return result, index + 1
            if key == "{":
                raise VdfParseError("Object key is missing")
            index += 1
            if index >= len(tokens):
                raise VdfParseError("Value is missing")
            value = tokens[index]
            if value == "{":
                parsed, index = parse_object(index + 1, True)
                result[key] = parsed
            elif value == "}":
                raise VdfParseError("Value is missing")
            else:
                result[key] = value
                index += 1
        if nested:
            raise VdfParseError("Unclosed object")
        return result, index

    parsed, end = parse_object(0, False)
    if end != len(tokens):
        raise VdfParseError("Unexpected trailing VDF data")
    return parsed


def read_vdf(path: Path) -> dict[str, object]:
    return parse_vdf(path.read_text(encoding="utf-8-sig"))
