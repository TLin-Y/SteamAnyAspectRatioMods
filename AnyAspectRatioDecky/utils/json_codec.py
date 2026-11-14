from collections.abc import Callable, Iterable
from dataclasses import MISSING, Field, dataclass, field, fields, is_dataclass
from enum import Enum
from functools import cache
from types import NoneType, UnionType
from typing import Annotated, Any, TypeVar, Union, get_args, get_origin, get_type_hints

from helper.errors import PluginError

T = TypeVar("T")
Decoder = Callable[[object], Any]

_OPTIONS = "json_codec"
_ABSENT = object()
# Dataclasses whose JSON objects may not carry keys other than their fields.
_EXACT_KEYS: set[type] = set()


class DecodeError(PluginError):
    def __init__(self, problem: str) -> None:
        super().__init__(problem)
        self.problem = problem
        self._location: list[str] = []

    def at(self, segment: str) -> "DecodeError":
        self._location.append(segment)
        return self

    def __str__(self) -> str:
        path = "".join(reversed(self._location)).lstrip(".")
        return f"{path or 'value'} {self.problem}"


@dataclass(frozen=True, slots=True)
class Check:
    is_valid: Callable[[Any], bool]
    expected: str


def one_of(*options: object) -> Check:
    return Check(frozenset(options).__contains__, _one_of_text(options))


def empty_or(check: Check) -> Check:
    return Check(lambda value: not value or check.is_valid(value), f"empty or {check.expected}")


@dataclass(frozen=True, slots=True)
class _FieldOptions:
    key: str | None
    wire: Any
    convert: Callable[[Any], Any] | None
    skip: bool


def json_field(
    key: str | None = None,
    *,
    default: Any = MISSING,
    wire: Any = None,
    convert: Callable[[Any], Any] | None = None,
    skip: bool = False,
) -> Any:
    return field(default=default, metadata={_OPTIONS: _FieldOptions(key, wire, convert, skip)})


def exact_keys(cls: type[T]) -> type[T]:
    _EXACT_KEYS.add(cls)
    return cls


def decode(value: object, target: type[T]) -> T:
    return _decoder(target)(value)

@cache
def _decoder(target: Any) -> Decoder:
    builder = _BUILDERS.get(get_origin(target))
    if builder is not None:
        return builder(target)
    if is_dataclass(target):
        return _dataclass_decoder(target)
    if isinstance(target, type) and issubclass(target, Enum):
        return _enum_decoder(target)
    if target is object or target is Any:
        return _unchanged
    if target in _SCALAR_NAMES:
        return _scalar_decoder(target)
    raise TypeError(f"json_codec cannot decode {target!r}")


def _unchanged(value: object) -> object:
    return value


def _scalar_decoder(target: type) -> Decoder:
    expected = _SCALAR_NAMES[target]

    def decode_scalar(value: object) -> object:
        # Exact type: JSON true is not an integer, 1 is not true.
        if type(value) is not target:
            raise _mismatch(expected, value)
        return value

    return decode_scalar


def _enum_decoder(target: type[Enum]) -> Decoder:
    members = {member.value: member for member in target}
    expected = _one_of_text(members)

    def decode_enum(value: object) -> Enum:
        member = members.get(value) if type(value) is str else None
        if member is None:
            raise _mismatch(expected, value)
        return member

    return decode_enum


def _annotated_decoder(target: Any) -> Decoder:
    base, *extras = get_args(target)
    decode_base = _decoder(base)
    checks = tuple(extra for extra in extras if isinstance(extra, Check))

    def decode_checked(value: object) -> object:
        result = decode_base(value)
        for check in checks:
            if not check.is_valid(result):
                raise _mismatch(check.expected, value)
        return result

    return decode_checked


def _optional_decoder(target: Any) -> Decoder:
    present = [option for option in get_args(target) if option is not NoneType]
    if len(present) != 1:
        raise TypeError(f"json_codec only decodes unions of one type and None: {target!r}")
    decode_present = _decoder(present[0])

    def decode_optional(value: object) -> object:
        return None if value is None else decode_present(value)

    return decode_optional


def _tuple_decoder(target: Any) -> Decoder:
    arguments = get_args(target)
    if len(arguments) != 2 or arguments[1] is not Ellipsis:
        raise TypeError(f"json_codec only decodes tuple[X, ...]: {target!r}")
    decode_item = _decoder(arguments[0])

    def decode_tuple(value: object) -> tuple:
        if type(value) is not list:
            raise _mismatch("a list", value)
        items = []
        for index, item in enumerate(value):
            try:
                items.append(decode_item(item))
            except DecodeError as error:
                raise error.at(f"[{index}]") from None
        return tuple(items)

    return decode_tuple


def _dict_decoder(target: Any) -> Decoder:
    key_type, value_type = get_args(target)
    decode_key, decode_value = _decoder(key_type), _decoder(value_type)

    def decode_dict(value: object) -> dict:
        if type(value) is not dict:
            raise _mismatch("an object", value)
        result = {}
        for key, item in value.items():
            try:
                result[decode_key(key)] = decode_value(item)
            except DecodeError as error:
                raise error.at(f"[{key!r}]") from None
        return result

    return decode_dict


@dataclass(frozen=True, slots=True)
class _FieldPlan:
    name: str
    key: str
    decode: Decoder
    convert: Callable[[Any], Any] | None
    # What a missing key means: _ABSENT = required, MISSING = use the dataclass default, else that value.
    when_missing: object


def _field_plan(item: Field, hint: Any) -> _FieldPlan | None:
    options: _FieldOptions = item.metadata.get(_OPTIONS) or _FieldOptions(None, None, None, False)
    if options.skip:
        return None
    if item.default is not MISSING or item.default_factory is not MISSING:
        when_missing = MISSING
    elif get_origin(hint) in (Union, UnionType) and NoneType in get_args(hint):
        when_missing = None
    else:
        when_missing = _ABSENT
    return _FieldPlan(
        name=item.name,
        key=options.key or item.name,
        decode=_decoder(options.wire or hint),
        convert=options.convert,
        when_missing=when_missing,
    )


def _dataclass_decoder(cls: type) -> Decoder:
    hints = get_type_hints(cls, include_extras=True)
    plans = tuple(
        plan for item in fields(cls) if (plan := _field_plan(item, hints[item.name])) is not None
    )
    known_keys = frozenset(plan.key for plan in plans)
    is_exact = cls in _EXACT_KEYS

    def decode_object(value: object) -> object:
        if type(value) is not dict:
            raise _mismatch("an object", value)
        if is_exact and not known_keys.issuperset(value):
            unknown = min(set(value) - known_keys)
            raise DecodeError(f"has an unknown key {unknown!r}")
        arguments = {}
        for plan in plans:
            raw = value.get(plan.key, _ABSENT)
            if raw is _ABSENT:
                if plan.when_missing is _ABSENT:
                    raise DecodeError("is missing").at(f".{plan.key}")
                if plan.when_missing is not MISSING:
                    arguments[plan.name] = plan.when_missing
                continue
            try:
                decoded = plan.decode(raw)
            except DecodeError as error:
                raise error.at(f".{plan.key}") from None
            arguments[plan.name] = decoded if plan.convert is None else plan.convert(decoded)
        return cls(**arguments)

    return decode_object


_BUILDERS: dict[Any, Callable[[Any], Decoder]] = {
    Annotated: _annotated_decoder,
    Union: _optional_decoder,
    UnionType: _optional_decoder,
    tuple: _tuple_decoder,
    dict: _dict_decoder,
}

_SCALAR_NAMES: dict[type, str] = {
    str: "a string",
    int: "an integer",
    bool: "true or false",
}


def _one_of_text(options: Iterable[object]) -> str:
    return "one of " + ", ".join(repr(getattr(option, "value", option)) for option in options)


def _mismatch(expected: str, value: object) -> DecodeError:
    return DecodeError(f"must be {expected}, got {_shown(value)}")


def _shown(value: object) -> str:
    if type(value) is dict:
        return "an object"
    if type(value) is list:
        return "a list"
    return "null" if value is None else repr(value)
