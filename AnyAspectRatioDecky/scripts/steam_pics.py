"""Anonymous Steam PICS lookup of public app info (install folder, launch entries), stdlib only."""

import base64
import gzip
import json
import os
import socket
import ssl
import struct
import urllib.request

CM_LIST = "https://api.steampowered.com/ISteamDirectory/GetCMListForConnect/v1/?cellid=0&format=json"
BATCH = 25
PROTO = 0x80000000
EMSG_MULTI = 1
EMSG_LOGOFF = 706
EMSG_LOGON_RESPONSE = 751
EMSG_LOGON = 5514
EMSG_INFO_REQUEST = 8903
EMSG_INFO_RESPONSE = 8904
EMSG_TOKENS_REQUEST = 8905
EMSG_TOKENS_RESPONSE = 8906
PROTOCOL_VERSION = 65580
ANON_STEAMID = (1 << 56) | (10 << 52)
MAX_FRAME = 32 * 1024 * 1024


class PicsError(Exception):
    pass


def _varint(value: int) -> bytes:
    out = bytearray()
    while value > 0x7F:
        out.append(value & 0x7F | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def _read_varint(data: bytes, i: int) -> tuple[int, int]:
    value = shift = 0
    while True:
        byte = data[i]
        i += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return value, i


class Proto:
    def __init__(self):
        self.out = bytearray()

    def varint(self, field: int, value: int) -> "Proto":
        self.out += _varint(field << 3) + _varint(value)
        return self

    def fixed64(self, field: int, value: int) -> "Proto":
        self.out += _varint(field << 3 | 1) + struct.pack("<Q", value)
        return self

    def bytes(self, field: int, value: bytes) -> "Proto":
        self.out += _varint(field << 3 | 2) + _varint(len(value)) + value
        return self


def fields(data: bytes) -> dict[int, list]:
    out: dict[int, list] = {}
    i = 0
    while i < len(data):
        key, i = _read_varint(data, i)
        wire = key & 7
        if wire == 0:
            value, i = _read_varint(data, i)
        elif wire == 1:
            value, i = struct.unpack_from("<Q", data, i)[0], i + 8
        elif wire == 5:
            value, i = struct.unpack_from("<I", data, i)[0], i + 4
        elif wire == 2:
            size, i = _read_varint(data, i)
            value, i = data[i:i + size], i + size
        else:
            raise PicsError(f"protobuf wire type {wire}")
        out.setdefault(key >> 3, []).append(value)
    return out


def _first(parsed: dict[int, list], field: int, default=0):
    return parsed.get(field, [default])[0]


def _packed(data: bytes) -> list[int]:
    out, i = [], 0
    while i < len(data):
        value, i = _read_varint(data, i)
        out.append(value)
    return out


def _header(steam_id: int, session: int) -> bytes:
    return Proto().fixed64(1, steam_id).varint(2, session).out


def _message(emsg: int, header: bytes, body: bytes) -> bytes:
    return struct.pack("<II", emsg | PROTO, len(header)) + header + body


def messages(data: bytes) -> list[tuple[int, bytes, bytes]]:
    if len(data) < 8:
        return []
    raw, header_size = struct.unpack_from("<II", data)
    if not raw & PROTO or 8 + header_size > len(data):
        return []
    emsg = raw & ~PROTO
    header, body = data[8:8 + header_size], data[8 + header_size:]
    if emsg != EMSG_MULTI:
        return [(emsg, header, body)]
    multi = fields(body)
    inner = _first(multi, 2, b"")
    if _first(multi, 1):
        inner = gzip.decompress(inner)
    out, i = [], 0
    while i + 4 <= len(inner):
        (size,) = struct.unpack_from("<I", inner, i)
        i += 4
        if size > len(inner) - i:
            break
        out += messages(inner[i:i + size])
        i += size
    return out


def parse_keyvalues(text: str) -> dict:
    """Parse Valve's text KeyValues; later duplicate keys win."""
    i, n = 0, len(text)

    def skip():
        nonlocal i
        while i < n:
            if text[i].isspace() or text[i] == "\0":
                i += 1
            elif text.startswith("//", i):
                while i < n and text[i] != "\n":
                    i += 1
            else:
                break

    def token() -> str:
        nonlocal i
        if text[i] == '"':
            i += 1
            out = []
            while i < n and text[i] != '"':
                if text[i] == "\\" and i + 1 < n:
                    i += 1
                    out.append({"n": "\n", "t": "\t"}.get(text[i], text[i]))
                else:
                    out.append(text[i])
                i += 1
            i += 1
            return "".join(out)
        start = i
        while i < n and not text[i].isspace() and text[i] not in "{}":
            i += 1
        if start == i:
            raise PicsError(f"KeyValues token expected at {i}")
        return text[start:i]

    def block(top: bool) -> dict:
        nonlocal i
        out: dict = {}
        while True:
            skip()
            if i >= n:
                if top:
                    return out
                raise PicsError("KeyValues block not closed")
            if text[i] == "}":
                i += 1
                return out
            key = token()
            skip()
            if i < n and text[i] == "{":
                i += 1
                out[key] = block(False)
            else:
                out[key] = token()

    return block(True)


class _WebSocket:
    def __init__(self, host: str, timeout: float):
        self.sock = ssl.create_default_context().wrap_socket(
            socket.create_connection((host, 443), timeout=timeout), server_hostname=host)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f"GET /cmsocket/ HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\n"
                           f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                           "Sec-WebSocket-Version: 13\r\n\r\n").encode())
        self.buffer = b""
        while b"\r\n\r\n" not in self.buffer:
            self._fill()
        head, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        status = head.split(b"\r\n", 1)[0]
        if b" 101 " not in status:
            raise PicsError("websocket upgrade refused: " + status.decode(errors="replace"))

    def _fill(self):
        chunk = self.sock.recv(65536)
        if not chunk:
            raise PicsError("websocket closed")
        self.buffer += chunk

    def _read(self, size: int) -> bytes:
        while len(self.buffer) < size:
            self._fill()
        out, self.buffer = self.buffer[:size], self.buffer[size:]
        return out

    def _frame(self, opcode: int, payload: bytes):
        size = len(payload)
        if size < 126:
            head = struct.pack("!BB", 0x80 | opcode, 0x80 | size)
        elif size < 65536:
            head = struct.pack("!BBH", 0x80 | opcode, 0x80 | 126, size)
        else:
            head = struct.pack("!BBQ", 0x80 | opcode, 0x80 | 127, size)
        mask = os.urandom(4)
        self.sock.sendall(head + mask + bytes(b ^ mask[k % 4] for k, b in enumerate(payload)))

    def send(self, payload: bytes):
        self._frame(0x2, payload)

    def receive(self) -> bytes:
        message = bytearray()
        while True:
            b0, b1 = self._read(2)
            size = b1 & 0x7F
            if size == 126:
                (size,) = struct.unpack("!H", self._read(2))
            elif size == 127:
                (size,) = struct.unpack("!Q", self._read(8))
            if size > MAX_FRAME:
                raise PicsError("websocket frame too large")
            data = self._read(size)
            opcode = b0 & 0x0F
            if opcode == 0x8:
                raise PicsError("websocket closed by server")
            if opcode == 0x9:
                self._frame(0xA, data)
            elif opcode in (0x0, 0x2):
                message += data
                if b0 & 0x80:
                    return bytes(message)

    def close(self):
        try:
            self._frame(0x8, b"")
        except OSError:
            pass
        self.sock.close()


def _cm_host(timeout: float) -> str:
    with urllib.request.urlopen(CM_LIST, timeout=timeout) as response:
        servers = json.load(response)["response"]["serverlist"]
    for server in servers:
        if server.get("type") == "websockets":
            return server["endpoint"].split(":")[0]
    raise PicsError("no websocket CM server listed")


def _receive(ws: _WebSocket, wanted: int):
    while True:
        for emsg, header, body in messages(ws.receive()):
            if emsg == wanted:
                yield fields(header), fields(body)


def lookup(app_ids: list[int], timeout: float = 20.0) -> dict[int, dict | None]:
    """Return {appid: appinfo KeyValues tree}; None for ids Steam does not know."""
    ws = _WebSocket(_cm_host(timeout), timeout)
    try:
        ws.send(_message(EMSG_LOGON, _header(ANON_STEAMID, 0),
                         Proto().varint(1, PROTOCOL_VERSION).varint(7, 16).bytes(6, b"english").out))
        header, body = next(_receive(ws, EMSG_LOGON_RESPONSE))
        if _first(body, 1) != 1:
            raise PicsError(f"anonymous logon refused (EResult {_first(body, 1)})")
        session = _header(_first(header, 1), _first(header, 2))

        out: dict[int, dict | None] = {}
        ids = list(dict.fromkeys(app_ids))
        for start in range(0, len(ids), BATCH):
            batch = ids[start:start + BATCH]
            request = Proto()
            for app_id in batch:
                request.varint(2, app_id)
            ws.send(_message(EMSG_TOKENS_REQUEST, session, request.out))
            _, body = next(_receive(ws, EMSG_TOKENS_RESPONSE))
            tokens = {_first(token, 1): _first(token, 2) for token in map(fields, body.get(3, []))}

            request = Proto()
            for app_id in batch:
                app = Proto().varint(1, app_id)
                if tokens.get(app_id):
                    app.varint(2, tokens[app_id])
                request.bytes(2, app.out)
            ws.send(_message(EMSG_INFO_REQUEST, session, request.varint(3, 0).out))
            for _, body in _receive(ws, EMSG_INFO_RESPONSE):
                for app in map(fields, body.get(1, [])):
                    text = _first(app, 5, b"").decode("utf-8", errors="replace").rstrip("\0 \r\n\t")
                    out[_first(app, 1)] = parse_keyvalues(text).get("appinfo") if text else None
                for unknown in body.get(2, []):
                    for app_id in [unknown] if isinstance(unknown, int) else _packed(unknown):
                        out[app_id] = None
                if not _first(body, 6):
                    break
            for app_id in batch:
                out.setdefault(app_id, None)
        try:
            ws.send(_message(EMSG_LOGOFF, session, b""))
        except OSError:
            pass
        return out
    finally:
        ws.close()
