import json
import re
from pathlib import Path

from helper.errors import PluginError
from utils import text_files

_TOKEN_PATTERN = re.compile(r'"((?:\\.|[^"\\])*)"|(\{|\})|//[^\r\n]*|([^\s{}"]+)')



class VdfParseError(PluginError):
    pass


def parse_vdf(text: str) -> dict[str, object]:
    tokens: list[str] = []
    position = 0
    while position < len(text):
        token = _TOKEN_PATTERN.match(text, position)
        if token is None:
            if text[position].isspace():
                position += 1
                continue
            raise VdfParseError(f"Unexpected character at offset {position}")
        position = token.end()

        match token.groups():
            case (str() as quoted, None, None):
                try:
                    tokens.append(json.loads(f'"{quoted}"'))
                except json.JSONDecodeError as error:
                    raise VdfParseError("Invalid escape in quoted VDF value") from error
            case (None, str() as symbol, None) | (None, None, str() as symbol):
                tokens.append(symbol)

    def parse_object(index: int, nested: bool) -> tuple[dict[str, object], int]:
        result: dict[str, object] = {}
        while index < len(tokens):
            key = tokens[index]

            match key:
                case "}" if nested:
                    return result, index + 1
                case "}":
                    raise VdfParseError("Unexpected closing brace")
                case "{":
                    raise VdfParseError("Object key is missing")
                
            index += 1
            if index >= len(tokens):
                raise VdfParseError("Value is missing")
            
            match tokens[index]:
                case "{":
                    result[key], index = parse_object(index + 1, True)
                case "}":
                    raise VdfParseError("Value is missing")
                case value:
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
    return parse_vdf(path.read_text(encoding=text_files.BOM_TEXT_ENCODING))
