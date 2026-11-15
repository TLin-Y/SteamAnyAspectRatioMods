import json
import re
import struct
from enum import IntEnum
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


class _BinaryType(IntEnum):
    """The byte that starts each entry of a binary VDF file (shortcuts.vdf)."""
    MAP = 0x00
    STRING = 0x01
    INT32 = 0x02
    END = 0x08


_UINT32 = struct.Struct("<I")


def _read_cstring(data: bytes, index: int) -> tuple[str, int]:
    end = data.find(b"\x00", index)
    if end < 0:
        raise VdfParseError("Unterminated binary VDF string")
    return data[index:end].decode(text_files.TEXT_ENCODING, errors="replace"), end + 1


def _read_uint32(data: bytes, index: int) -> tuple[int, int]:
    if index + _UINT32.size > len(data):
        raise VdfParseError("Truncated binary VDF number")
    return _UINT32.unpack_from(data, index)[0], index + _UINT32.size


def parse_binary_vdf(data: bytes) -> dict[str, object]:
    
    def parse_map(index: int) -> tuple[dict[str, object], int]:
        result: dict[str, object] = {}
        while index < len(data):
            kind, index = data[index], index + 1
            if kind == _BinaryType.END:
                return result, index
            key, index = _read_cstring(data, index)
            match kind:
                case _BinaryType.MAP:
                    result[key], index = parse_map(index)
                case _BinaryType.STRING:
                    result[key], index = _read_cstring(data, index)
                case _BinaryType.INT32:
                    result[key], index = _read_uint32(data, index)
                case _:
                    raise VdfParseError(f"Unsupported binary VDF type {kind}")
        raise VdfParseError("Unclosed binary VDF map")

    parsed, _ = parse_map(0)
    return parsed
