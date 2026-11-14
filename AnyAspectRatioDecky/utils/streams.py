"""Size-limited reads and copies between open binary streams."""

from helper.errors import PluginError
from utils import sizes

CHUNK_BYTES = sizes.MIB


def read_limited(stream, max_bytes: int, description: str) -> bytes:
    content = stream.read(max_bytes + 1)
    if len(content) > max_bytes:
        raise PluginError(f"{description} exceeds the {sizes.label(max_bytes)} limit")
    return content


def copy_limited(stream, target, max_bytes: int, description: str) -> int:
    total_bytes = 0
    while True:
        chunk = stream.read(min(CHUNK_BYTES, max_bytes - total_bytes + 1))
        if not chunk:
            return total_bytes
        total_bytes += len(chunk)
        if total_bytes > max_bytes:
            raise PluginError(f"{description} exceeds the {sizes.label(max_bytes)} limit")
        target.write(chunk)
