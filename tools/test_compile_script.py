#!/usr/bin/env python3
"""Unit tests for tools/compile_script.py string syntax (<runes>/<text>).

Run: python3 tools/test_compile_script.py
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compile_script as cs  # noqa: E402


def encode(source: str) -> bytes:
    """Encode a string literal body exactly as the compiler does."""
    return cs.encode_string(cs.decode_string(f'"{source}"', 1))


def compile_script_text(source: str) -> bytes:
    decls = cs.Parser(cs.tokenize(source), {}).parse_program()
    assert len(decls) == 1
    return cs.compile_decl(decls[0])


class StringSyntax(unittest.TestCase):
    def test_plain_text_is_unchanged(self):
        # a-z -> $41-$5A, A-Z -> $C1-$DA, everything else as-is.
        self.assertEqual(encode("aZ 1!"), bytes([0x41, 0xDA, 0x20, 0x31, 0x21]))

    def test_runes_then_text(self):
        # a b -> runes $81 $82; the trailing latin 'a' is back to $41.
        self.assertEqual(encode("<runes>ab<text>a"), bytes([0x81, 0x82, 0x41]))

    def test_rune_range_and_case_insensitivity(self):
        self.assertEqual(encode("<runes>az"), bytes([0x81, 0x9A]))
        self.assertEqual(encode("<runes>AZ"), bytes([0x81, 0x9A]))
        self.assertEqual(encode("<runes>abcdefghijklmnopqrstuvwxyz"),
                         bytes(range(0x81, 0x9B)))

    def test_non_letters_unchanged_in_rune_mode(self):
        self.assertEqual(encode("<runes>a b, 1!\\nc"),
                         bytes([0x81, 0x20, 0x82, 0x2C, 0x20, 0x31, 0x21,
                                0x0A, 0x83]))

    def test_tags_mid_string(self):
        # x, then rune y ($81 + 24 = $99), then z back in text mode.
        self.assertEqual(encode("x<runes>y<text>z"), bytes([0x58, 0x99, 0x5A]))

    def test_escaped_less_than_is_literal(self):
        self.assertEqual(encode("a \\< b"), bytes([0x41, 0x20, 0x3C, 0x20, 0x42]))
        # Text that looks like a tag is literal when the '<' is escaped:
        # '<', then "runes" as ordinary lowercase text, then '>'.
        self.assertEqual(encode("\\<runes>"),
                         bytes([0x3C, 0x52, 0x55, 0x4E, 0x45, 0x53, 0x3E]))

    def test_unknown_or_malformed_tag_is_an_error(self):
        for bad in ("<runs>x", "a < b", "<Runes>x", "<runes"):
            with self.subTest(bad=bad), self.assertRaises(SystemExit):
                encode(bad)

    def test_mode_ends_with_the_string(self):
        # The first string never switches back; the second must still start
        # in text mode ("ab" -> $41 $42), not inherit rune mode.
        code = compile_script_text('script s { text "<runes>ab" text "ab" }')
        self.assertIn(bytes([0x81, 0x82, 0x00]), code)
        self.assertIn(bytes([0x41, 0x42, 0x00]), code)

    def test_runic_and_plain_strings_do_not_merge(self):
        code = compile_script_text('script s { text "ab" text "<runes>ab" }')
        self.assertIn(bytes([0x41, 0x42, 0x00]), code)
        self.assertIn(bytes([0x81, 0x82, 0x00]), code)

    def test_topic_keyword_rejects_tags(self):
        with self.assertRaises(SystemExit):
            compile_script_text(
                'conversation c { topic "<runes>hi" { text "x" } }')


if __name__ == "__main__":
    unittest.main()
