import unittest
from dataclasses import dataclass
from enum import StrEnum
from typing import Annotated

from utils.json_codec import Check, DecodeError, decode, empty_or, exact_keys, json_field, one_of

Even = Annotated[int, Check(lambda value: value % 2 == 0, "an even number")]


class Color(StrEnum):
    RED = "red"
    BLUE = "blue"
    GREEN = "green"


Primary = Annotated[Color, one_of(Color.RED, Color.BLUE)]


@dataclass(frozen=True, slots=True)
class Point:
    x: int
    y: int = 0


@exact_keys
@dataclass(frozen=True, slots=True)
class Strict:
    name: str


@dataclass(frozen=True, slots=True)
class Shape:
    shape_id: str = json_field("id")
    points: tuple[Point, ...]
    label: str | None
    tags: dict[str, Even] | None = None
    kind: Annotated[str, one_of("open", "closed")] = "open"
    note: Annotated[str, empty_or(Check(str.isupper, "upper case"))] = ""
    sorted_tags: tuple[tuple[str, int], ...] = json_field(
        default=(), wire=dict[str, int], convert=lambda tags: tuple(sorted(tags.items())),
    )
    owner: str = json_field(default="nobody", skip=True)


class JsonCodecTests(unittest.TestCase):
    def assert_fails(self, document: object, message: str) -> None:
        with self.assertRaises(DecodeError) as caught:
            decode(document, Shape)
        self.assertEqual(message, str(caught.exception))

    def test_decodes_nested_fields_and_defaults(self) -> None:
        shape = decode(
            {
                "id": "a",
                "points": [{"x": 1, "y": 2}, {"x": 3, "extra": True}],
                "sorted_tags": {"b": 2, "a": 1},
                "owner": "ignored",
            },
            Shape,
        )
        self.assertEqual(
            Shape("a", (Point(1, 2), Point(3)), None, None, "open", "", (("a", 1), ("b", 2))),
            shape,
        )
        self.assertEqual("nobody", shape.owner)

    def test_errors_name_the_json_location(self) -> None:
        base = {"id": "a", "points": [], "label": None}
        self.assert_fails([], "value must be an object, got a list")
        self.assert_fails({"points": []}, "id is missing")
        self.assert_fails({**base, "points": [{"x": 1}, {"x": "2"}]}, "points[1].x must be an integer, got '2'")
        self.assert_fails({**base, "points": [{"x": True}]}, "points[0].x must be an integer, got True")
        self.assert_fails({**base, "label": 5}, "label must be a string, got 5")
        self.assert_fails({**base, "tags": {"a": 3}}, "tags['a'] must be an even number, got 3")
        self.assert_fails({**base, "kind": "round"}, "kind must be one of 'open', 'closed', got 'round'")
        self.assert_fails({**base, "note": "low"}, "note must be empty or upper case, got 'low'")
        self.assert_fails({**base, "points": None}, "points must be a list, got null")

    def test_exact_keys_rejects_unknown_keys(self) -> None:
        self.assertEqual(Strict("a"), decode({"name": "a"}, Strict))
        with self.assertRaisesRegex(DecodeError, "has an unknown key 'other'"):
            decode({"name": "a", "other": 1}, Strict)

    def test_enums_decode_from_their_values(self) -> None:
        self.assertIs(Color.RED, decode("red", Color))
        self.assertIs(Color.RED, decode("red", Primary))
        for value, message in (
            ("pink", "value must be one of 'red', 'blue', 'green', got 'pink'"),
            ("green", "value must be one of 'red', 'blue', got 'green'"),
            (["red"], "value must be one of 'red', 'blue', 'green', got a list"),
        ):
            with self.subTest(value=value), self.assertRaises(DecodeError) as caught:
                decode(value, Primary)
            self.assertEqual(message, str(caught.exception))

    def test_unsupported_types_fail_when_built(self) -> None:
        for target in (float, int | str, tuple[int, str]):
            with self.subTest(target=target), self.assertRaises(TypeError):
                decode(1, target)


if __name__ == "__main__":
    unittest.main()
