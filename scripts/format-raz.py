#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Format Raz source files using the repository's canonical source style."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from path_policy import is_generated_dir

REPO_ROOT = Path(__file__).resolve().parents[1]

TOKEN_RE = re.compile(
    r"(?:<<=|>>=|\.\.=|==|!=|<=|>=|->|=>|::|\.\.|\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<|>>|&&|\|\|)|"
    r"(?:0[xX][0-9A-Fa-f_]+|0[bB][01_]+|0[oO][0-7_]+|[0-9][0-9_]*(?:\.[0-9][0-9_]*)?(?:[eE][+-]?[0-9][0-9_]*)?)|"
    r"[A-Za-z_][A-Za-z0-9_]*|"
    r"[^\s]"
)

TYPE_NAMES = {
    "bool",
    "char",
    "string",
    "byte",
    "usize",
    "isize",
    "int",
    "uint",
    "i8",
    "i16",
    "i32",
    "i64",
    "i128",
    "u8",
    "u16",
    "u32",
    "u64",
    "u128",
    "f16",
    "f32",
    "f64",
    "Self",
    "auto",
}

NO_SPACE_BEFORE = {",", ";", ")", "]", ".", ":", "::", "..", "..="}
NO_SPACE_AFTER = {"(", "[", ".", "::", "..", "..="}
BINARY_OPERATORS = {
    "=",
    "==",
    "!=",
    "<",
    "<=",
    ">",
    ">=",
    "<<",
    ">>",
    "+",
    "-",
    "*",
    "/",
    "%",
    "&",
    "|",
    "^",
    "+=",
    "-=",
    "*=",
    "/=",
    "%=",
    "&=",
    "|=",
    "^=",
    "<<=",
    ">>=",
    "&&",
    "||",
    "->",
    "=>",
}

DECLARATION_PREFIXES = (
    "public enum ",
    "enum ",
    "public struct ",
    "struct ",
    "trait ",
    "public trait ",
    "impl ",
    "extern fn ",
    "fn ",
    "public fn ",
    "public unsafe fn ",
    'extern "',
)


@dataclass
class Token:
    text: str
    start: int
    end: int
    generic_open: bool = False
    generic_close: bool = False
    pointer_star: bool = False
    deref_star: bool = False
    amp_prefix: bool = False
    amp_postfix: bool = False
    unary: bool = False


def _scan_tokens(text: str) -> list[Token]:
    """Tokenize enough of Raz to make whitespace decisions safely."""
    tokens: list[Token] = []
    index = 0

    while index < len(text):
        if text[index].isspace():
            index += 1
            continue

        if text.startswith("//", index):
            end = text.find("\n", index)
            if end < 0:
                end = len(text)
            tokens.append(Token(text[index:end], index, end))
            index = end
            continue

        if text.startswith("/*", index):
            start = index
            index += 2
            depth = 1
            while index < len(text) and depth:
                if text.startswith("/*", index):
                    depth += 1
                    index += 2
                elif text.startswith("*/", index):
                    depth -= 1
                    index += 2
                else:
                    index += 1
            tokens.append(Token(text[start:index], start, index))
            continue

        if text[index] in {'"', "'"}:
            quote = text[index]
            start = index
            index += 1
            escaped = False
            while index < len(text):
                char = text[index]
                index += 1
                if escaped:
                    escaped = False
                    continue
                if char == "\\":
                    escaped = True
                    continue
                if char == quote:
                    break
            tokens.append(Token(text[start:index], start, index))
            continue

        match = TOKEN_RE.match(text, index)
        if match is None:
            tokens.append(Token(text[index], index, index + 1))
            index += 1
            continue

        tokens.append(Token(match.group(0), match.start(), match.end()))
        index = match.end()

    return tokens


def _looks_like_type(token: Token | None) -> bool:
    if token is None:
        return False
    return (
        token.text in TYPE_NAMES
        or token.generic_close
        or bool(token.text and token.text[0].isupper())
    )


def tokenize(text: str) -> list[Token]:
    """Classify punctuation whose meaning depends on source adjacency."""
    raw_tokens = _scan_tokens(text)
    tokens: list[Token] = []
    generic_depth = 0

    for index, token in enumerate(raw_tokens):
        previous = tokens[-1] if tokens else None
        next_token = raw_tokens[index + 1] if index + 1 < len(raw_tokens) else None

        # Raz intentionally reuses < and > for comparisons and generic arguments.
        # Source adjacency is a reliable formatter hint without duplicating the parser.
        if (
            token.text == "<"
            and previous is not None
            and previous.end == token.start
            and (
                re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", previous.text) is not None
                or previous.generic_close
            )
        ):
            token.generic_open = True
            generic_depth += 1
        elif token.text == ">>" and generic_depth >= 2:
            # Keep the lexer token intact. `>>` is valid both as a shift operator and
            # as two adjacent generic closers; splitting it here can corrupt shifts
            # when an earlier comparison was conservatively classified as generic.
            # The compact spelling is correct in either case.
            generic_depth -= 2
        elif token.text == ">" and generic_depth > 0:
            token.generic_close = True
            generic_depth -= 1

        previous = tokens[-1] if tokens else None

        if (
            token.text == "*"
            and previous is not None
            and next_token is not None
            and previous.end == token.start
            and next_token.text in {"mut", "const"}
        ):
            token.pointer_star = True
        elif token.text == "*" and next_token is not None and (
            token.end == next_token.start
            or previous is None
            or previous.text
            in {
                "{",
                ";",
                "(",
                "[",
                ",",
                "=",
                "return",
                "=>",
                "+",
                "-",
                "*",
                "/",
                "%",
                "&",
                "|",
                "^",
                "&&",
                "||",
            }
        ):
            token.deref_star = True

        if token.text == "&":
            if (
                previous is not None
                and previous.end == token.start
                and _looks_like_type(previous)
            ):
                token.amp_postfix = True
            elif next_token is not None and (
                token.end == next_token.start or next_token.text == "mut"
            ):
                token.amp_prefix = True

        if token.text in {"-", "+", "!", "~"}:
            previous_text = previous.text if previous else None
            if previous is None or previous_text in {
                "(",
                "[",
                "{",
                ",",
                "=",
                "return",
                "=>",
                ":",
                "+",
                "-",
                "*",
                "/",
                "%",
                "!",
                "~",
                "&&",
                "||",
                "<",
                ">",
                "<=",
                ">=",
                "==",
                "!=",
            }:
                token.unary = True

        tokens.append(token)

    return tokens


def needs_space(previous: Token | None, current: Token) -> bool:
    """Return whether a single space belongs between two formatted tokens."""
    if previous is None:
        return False

    left = previous.text
    right = current.text

    # Raz 1.0 currently tokenizes `>>` as a shift outside parser context. Keep
    # adjacent generic closes separated (`Outer<Inner<T> >`) so formatted source
    # remains unambiguous to both the bootstrap and Raz-written parsers.
    if previous.generic_close and current.generic_close:
        return True
    if current.generic_open or previous.generic_open or current.generic_close:
        return False
    if previous.generic_close and right in {"(", ".", "::"}:
        return False
    if current.pointer_star or previous.pointer_star:
        return False
    if current.deref_star:
        return left in {"return", "if", "while", ","} or left in BINARY_OPERATORS
    if previous.deref_star:
        return False
    if current.amp_postfix:
        return False
    if current.amp_prefix:
        return left in {"return", "if", "while", ","} or left in BINARY_OPERATORS
    if previous.amp_prefix:
        return False
    if previous.amp_postfix:
        return right != "mut"
    if previous.pointer_star and right in {"mut", "const"}:
        return False
    if left in {"mut", "const"} and right and (right[0].isalnum() or right[0] == "_"):
        # `i8*mut value` needs a separator after the pointer qualifier.
        return True
    if right in NO_SPACE_BEFORE or left in NO_SPACE_AFTER:
        return False
    if left == ":":
        return True
    if right == "(" and left in {"if", "while", "for", "match", "switch", "catch", "return"}:
        return True
    if right == "(" and (left[0].isalnum() or left[0] == "_" or previous.generic_close):
        return False
    if right == "{" or left == "}":
        return True
    if right == "!" and left in {"if", "while"}:
        return True
    if current.unary:
        # Unary operators hug their operand, while remaining visually separated
        # from a preceding keyword or binary operator: `return -1`, `a && !b`.
        return left in {"return", "if", "while", ","} or left in BINARY_OPERATORS
    if previous.unary:
        return False
    if right in BINARY_OPERATORS or left in BINARY_OPERATORS:
        return True
    if left == ",":
        return True

    return (left[-1].isalnum() or left[-1] == "_") and (
        right[0].isalnum() or right[0] == "_"
    )


def _separate_top_level_declarations(lines: list[str]) -> list[str]:
    """Add one blank line between top-level declarations without orphaning comments."""
    output: list[str] = []

    for line in lines:
        is_top_level = bool(line) and not line.startswith(" ")
        starts_declaration = line.startswith(DECLARATION_PREFIXES)
        follows_declaration = bool(output) and output[-1] == "}" and not line.startswith("else")

        if is_top_level and output and output[-1] and (starts_declaration or follows_declaration):
            previous_line = output[-1].lstrip()
            comment_attached = previous_line.startswith(("//", "/*", "*"))
            grouped_externs = line.startswith("extern ") and previous_line.startswith("extern ")
            if not comment_attached and not grouped_externs:
                output.append("")

        output.append(line.rstrip())

    return output


def _style_control_flow(text: str) -> str:
    """Normalize ordinary if/while conditions while preserving concise negation syntax."""
    styled: list[str] = []
    control_re = re.compile(r"^(\s*)(if|while) (?![!(])(.+) \{$")
    else_if_re = re.compile(r"^(\s*} else if) (?![!(])(.+) \{$")

    for line in text.splitlines():
        match = control_re.match(line)
        if match:
            line = f"{match.group(1)}{match.group(2)} ({match.group(3)}) {{"
        else:
            match = else_if_re.match(line)
            if match:
                line = f"{match.group(1)} ({match.group(2)}) {{"
        styled.append(line)

    return "\n".join(styled).rstrip() + "\n"



FORMAT_WIDTH = 110
INDENT_WIDTH = 4


def _matching_paren(line: str, open_index: int) -> int:
    depth = 0
    quote = None
    escaped = False
    for index in range(open_index, len(line)):
        ch = line[index]
        if quote is not None:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == quote:
                quote = None
            continue
        if ch in {'"', "'"}:
            quote = ch
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return index
    return -1


def _split_top_level(text: str, separator: str = ",") -> list[str]:
    parts: list[str] = []
    start = 0
    paren = bracket = brace = angle = 0
    quote = None
    escaped = False
    index = 0
    while index < len(text):
        ch = text[index]
        if quote is not None:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == quote:
                quote = None
            index += 1
            continue
        if ch in {'"', "'"}:
            quote = ch
            index += 1
            continue
        if ch == "(": paren += 1
        elif ch == ")": paren = max(0, paren - 1)
        elif ch == "[": bracket += 1
        elif ch == "]": bracket = max(0, bracket - 1)
        elif ch == "{": brace += 1
        elif ch == "}": brace = max(0, brace - 1)
        elif ch == "<" and index > 0 and (text[index - 1].isalnum() or text[index - 1] in "_>"):
            angle += 1
        elif ch == ">" and angle > 0:
            angle -= 1
        elif ch == separator and paren == bracket == brace == angle == 0:
            parts.append(text[start:index].strip())
            start = index + 1
        index += 1
    tail = text[start:].strip()
    if tail:
        parts.append(tail)
    return parts




def _split_top_level_arithmetic(text: str) -> list[tuple[str, str]]:
    chunks: list[tuple[str, str]] = []
    start = 0
    paren = bracket = brace = 0
    quote = None
    escaped = False
    index = 0
    while index < len(text):
        ch = text[index]
        if quote is not None:
            if escaped: escaped = False
            elif ch == "\\": escaped = True
            elif ch == quote: quote = None
            index += 1
            continue
        if ch in {'"', "'"}:
            quote = ch; index += 1; continue
        if ch == "(": paren += 1
        elif ch == ")": paren = max(0, paren - 1)
        elif ch == "[": bracket += 1
        elif ch == "]": bracket = max(0, bracket - 1)
        elif ch == "{": brace += 1
        elif ch == "}": brace = max(0, brace - 1)
        if paren == bracket == brace == 0:
            if text.startswith(" + ", index) or text.startswith(" | ", index):
                op = text[index + 1]
                chunks.append((text[start:index].strip(), op))
                index += 3
                start = index
                continue
        index += 1
    tail = text[start:].strip()
    if tail:
        chunks.append((tail, ""))
    return chunks


def _split_boolean_condition(text: str) -> list[tuple[str, str]]:
    """Return condition chunks paired with the operator that follows each chunk."""
    chunks: list[tuple[str, str]] = []
    start = 0
    paren = bracket = brace = 0
    quote = None
    escaped = False
    index = 0
    while index < len(text) - 1:
        ch = text[index]
        if quote is not None:
            if escaped: escaped = False
            elif ch == "\\": escaped = True
            elif ch == quote: quote = None
            index += 1
            continue
        if ch in {'"', "'"}:
            quote = ch; index += 1; continue
        if ch == "(": paren += 1
        elif ch == ")": paren = max(0, paren - 1)
        elif ch == "[": bracket += 1
        elif ch == "]": bracket = max(0, bracket - 1)
        elif ch == "{": brace += 1
        elif ch == "}": brace = max(0, brace - 1)
        if paren == bracket == brace == 0 and text[index:index+2] in {"&&", "||"}:
            op = text[index:index+2]
            chunks.append((text[start:index].strip(), op))
            start = index + 2
            index += 2
            continue
        index += 1
    tail = text[start:].strip()
    if tail:
        chunks.append((tail, ""))
    return chunks


def _call_like_prefix(prefix: str) -> bool:
    head = prefix.rstrip()
    return bool(head) and (head[-1].isalnum() or head[-1] in "_>]" )


def _wrap_argument(arg: str, indent: str, width: int) -> list[str]:
    if len(indent) + len(arg) <= width:
        return [indent + arg]
    open_index = arg.find("(")
    if open_index < 0:
        return [indent + arg]
    close_index = _matching_paren(arg, open_index)
    if close_index < 0:
        return [indent + arg]
    inner = arg[open_index + 1:close_index]
    prefix_text = arg[:open_index]
    suffix = arg[close_index + 1:]

    # Parentheses used only for grouping are not comma-list constructs. Never
    # invent a trailing comma here, because `(expr,)` is a tuple in Raz.
    if not _call_like_prefix(prefix_text):
        chunks = _split_boolean_condition(inner)
        if len(chunks) <= 1:
            return [indent + arg]
        lines = [indent + prefix_text + "("]
        child_indent = indent + " " * INDENT_WIDTH
        for chunk, op in chunks:
            wrapped = _wrap_argument(chunk, child_indent, width)
            if op:
                wrapped[-1] += " " + op
            lines.extend(wrapped)
        lines.append(indent + ")" + suffix)
        return lines

    items = _split_top_level(inner)
    if not items:
        return [indent + arg]
    prefix = prefix_text + "("
    lines = [indent + prefix]
    child_indent = indent + " " * INDENT_WIDTH
    for item in items:
        wrapped = _wrap_argument(item, child_indent, width)
        wrapped[-1] += ","
        lines.extend(wrapped)
    lines.append(indent + ")" + suffix)
    return lines


def _wrap_line(line: str, width: int) -> list[str]:
    if len(line) <= width:
        return [line]
    stripped = line.lstrip()
    base = line[:len(line) - len(stripped)]

    # Long boolean returns are easier to scan as a parenthesized condition block.
    if stripped.startswith("return ") and stripped.endswith(";"):
        expression = stripped[len("return "):-1].strip()
        if expression.startswith("("):
            close = _matching_paren(expression, 0)
            if close == len(expression) - 1:
                grouped = expression[1:-1].strip()
                if len(_split_boolean_condition(grouped)) > 1:
                    expression = grouped
        chunks = _split_boolean_condition(expression)
        if len(chunks) > 1:
            lines = [base + "return ("]
            child = base + " " * INDENT_WIDTH
            for chunk, op in chunks:
                wrapped = _wrap_argument(chunk, child, width)
                if op:
                    wrapped[-1] += " " + op
                lines.extend(wrapped)
            lines.append(base + ");")
            return lines
        arithmetic = _split_top_level_arithmetic(expression)
        if len(arithmetic) > 1:
            first, first_op = arithmetic[0]
            lines = [base + "return " + first + (" " + first_op if first_op else "")]
            child = base + " " * INDENT_WIDTH
            for index, (chunk, op) in enumerate(arithmetic[1:]):
                suffix = (" " + op) if op else (";" if index == len(arithmetic) - 2 else "")
                lines.append(child + chunk + suffix)
            return lines

    # Split long declaration/assignment boolean expressions after `=` while
    # keeping the final semicolon on the final condition line.
    if stripped.endswith(";") and " = " in stripped:
        left, expression = stripped[:-1].split(" = ", 1)
        chunks = _split_boolean_condition(expression)
        if len(chunks) > 1:
            lines = [base + left + " ="]
            child = base + " " * INDENT_WIDTH
            for index, (chunk, op) in enumerate(chunks):
                wrapped = _wrap_argument(chunk, child, width)
                if op:
                    wrapped[-1] += " " + op
                elif index == len(chunks) - 1:
                    wrapped[-1] += ";"
                lines.extend(wrapped)
            return lines
        arithmetic = _split_top_level_arithmetic(expression)
        if len(arithmetic) > 1:
            lines = [base + left + " ="]
            child = base + " " * INDENT_WIDTH
            for index, (chunk, op) in enumerate(arithmetic):
                suffix = (" " + op) if op else (";" if index == len(arithmetic) - 1 else "")
                lines.append(child + chunk + suffix)
            return lines

    control = re.match(r"^(?P<head>(?:} else )?(?:if|while)) \((?P<body>.*)\) \{$", stripped)
    if control:
        chunks = _split_boolean_condition(control.group("body"))
        lines = [base + control.group("head") + " ("]
        child = base + " " * INDENT_WIDTH
        if len(chunks) > 1:
            for chunk, op in chunks:
                wrapped = _wrap_argument(chunk, child, width)
                if op:
                    wrapped[-1] += " " + op
                lines.extend(wrapped)
        else:
            lines.extend(_wrap_argument(control.group("body"), child, width))
        lines.append(base + ") {")
        return lines

    open_index = stripped.find("(")
    if open_index < 0:
        return [line]
    close_index = _matching_paren(stripped, open_index)
    if close_index < 0:
        return [line]
    inner = stripped[open_index + 1:close_index]
    items = _split_top_level(inner)
    if len(items) < 2:
        return [line]
    prefix = stripped[:open_index + 1]
    suffix = stripped[close_index + 1:]
    lines = [base + prefix]
    child = base + " " * INDENT_WIDTH
    for item in items:
        wrapped = _wrap_argument(item, child, width)
        wrapped[-1] += ","
        lines.extend(wrapped)
    lines.append(base + ")" + suffix)
    return lines




def _remove_compact_trailing_commas(text: str) -> str:
    output: list[str] = []
    for line in text.splitlines():
        stack: list[int] = []
        pairs: list[tuple[int, int]] = []
        quote = None
        escaped = False
        for index, ch in enumerate(line):
            if quote is not None:
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == quote:
                    quote = None
                continue
            if ch in {'"', "'"}:
                quote = ch
            elif ch == "(":
                stack.append(index)
            elif ch == ")" and stack:
                pairs.append((stack.pop(), index))

        deletions: list[tuple[int, int]] = []
        for open_index, close_index in pairs:
            prefix = line[:open_index]
            if not _call_like_prefix(prefix):
                continue
            cursor = close_index - 1
            while cursor > open_index and line[cursor].isspace():
                cursor -= 1
            if cursor > open_index and line[cursor] == ",":
                deletions.append((cursor, close_index))

        chars = line
        for begin, end in sorted(deletions, reverse=True):
            chars = chars[:begin] + chars[end:]
        output.append(chars)
    return "\n".join(output).rstrip() + "\n"


def _layout_width(text: str, width: int = FORMAT_WIDTH) -> str:
    output: list[str] = []
    for line in text.splitlines():
        output.extend(_wrap_line(line, width))
    return "\n".join(output).rstrip() + "\n"




def _line_indent(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def _is_local_declaration(line: str) -> bool:
    stripped = line.strip()
    if not stripped or not stripped.endswith(";"):
        return False
    if stripped.startswith(("return ", "break", "continue", "import ", "extern ")):
        return False
    return re.match(
        r"^(?:auto|[A-Za-z_][A-Za-z0-9_:]*(?:<[^;=]+>)?(?:\[\])?(?:&mut|&|\*mut|\*const)?)\s+"
        r"[A-Za-z_][A-Za-z0-9_]*(?:\[[^\]]*\])?\s*(?:=|;)",
        stripped,
    ) is not None


def _separate_logical_phases(text: str) -> str:
    lines = text.splitlines()
    output: list[str] = []
    previous_kind: bool | None = None
    previous_indent = -1
    for line in lines:
        if not line.strip():
            output.append("")
            previous_kind = None
            previous_indent = -1
            continue
        current_kind = _is_local_declaration(line)
        current_indent = _line_indent(line)
        if (
            output
            and output[-1].strip()
            and previous_kind is not None
            and current_indent == previous_indent
            and current_indent > 0
            and current_kind != previous_kind
            and not line.lstrip().startswith(("}", "else"))
            and not output[-1].lstrip().startswith(("if ", "while ", "for "))
        ):
            output.append("")
        output.append(line)
        previous_kind = current_kind
        previous_indent = current_indent
    return "\n".join(output).rstrip() + "\n"


def format_text(text: str) -> str:
    """Format one Raz source string while preserving the SPDX file header."""
    license_prefix = ""
    source = text
    header_lines = text.splitlines()
    if (
        len(header_lines) >= 2
        and header_lines[0].startswith("// Copyright ")
        and header_lines[1].startswith("// SPDX-License-Identifier: ")
    ):
        license_prefix = header_lines[0] + "\n" + header_lines[1] + "\n\n"
        source = "\n".join(header_lines[2:]).lstrip("\n")
        if source and not source.endswith("\n"):
            source += "\n"
    tokens = tokenize(source)
    lines: list[str] = []
    line = ""
    indent = 0
    paren_depth = 0
    bracket_depth = 0
    generic_depth = 0
    brace_paren_depths: list[int] = []
    previous: Token | None = None

    def flush() -> None:
        nonlocal line
        stripped = line.rstrip()
        if stripped:
            lines.append("    " * indent + stripped)
        line = ""

    for token in tokens:
        text_value = token.text

        if text_value.startswith("//"):
            if line.strip() == "}":
                flush()
            if line and not line.endswith(" "):
                line += " "
            line += text_value
            flush()
            previous = None
            continue

        if text_value.startswith("/*"):
            flush()
            for comment_line in text_value.splitlines():
                lines.append("    " * indent + comment_line.strip())
            previous = None
            continue

        if text_value == "{":
            if line and not line.endswith(" "):
                line += " "
            line += "{"
            flush()
            brace_paren_depths.append(paren_depth)
            indent += 1
            previous = None
            continue

        if text_value == "}":
            flush()
            indent = max(0, indent - 1)
            if brace_paren_depths:
                brace_paren_depths.pop()
            line = "}"
            previous = token
            continue

        if text_value == ";":
            line += ";"
            flush()
            previous = None
            continue

        if text_value == ",":
            line += ","
            field_level = bool(brace_paren_depths) and paren_depth == brace_paren_depths[-1]
            if (paren_depth == 0 or field_level) and bracket_depth == 0 and generic_depth == 0:
                flush()
                previous = None
            else:
                line += " "
                previous = token
            continue

        if token.generic_open:
            generic_depth += 1
        elif token.generic_close:
            generic_depth = max(0, generic_depth - 1)
        elif text_value == "(":
            paren_depth += 1
        elif text_value == ")":
            paren_depth = max(0, paren_depth - 1)
        elif text_value == "[":
            bracket_depth += 1
        elif text_value == "]":
            bracket_depth = max(0, bracket_depth - 1)

        if text_value == "else" and line == "}":
            line += " else"
            previous = token
            continue

        if line == "}" and text_value != "else":
            flush()

        if needs_space(previous, token) and line and not line.endswith(" "):
            line += " "
        line += text_value
        previous = token

    flush()

    formatted = "\n".join(_separate_top_level_declarations(lines)).rstrip() + "\n"

    # Preserve Raz's postfix type reference spelling and compact statement-start
    # dereferences even when input was written in older whitespace styles.
    formatted = re.sub(r"([A-Za-z0-9_])&(?!mut\b)([A-Za-z_])", r"\1& \2", formatted)
    formatted = re.sub(r"^(\s*)\* ([A-Za-z_])", r"\1*\2", formatted, flags=re.MULTILINE)
    # Empty declarations are clearer as `trait Marker {}` than as two lines
    # containing no body. Keep non-empty blocks expanded.
    formatted = re.sub(r" \{\n\s*\}", " {}", formatted)
    formatted = _remove_compact_trailing_commas(formatted)
    formatted = _layout_width(formatted)
    formatted = _separate_logical_phases(formatted)

    return license_prefix + formatted


def _is_generated_source(path: Path) -> bool:
    try:
        relative = path.resolve().relative_to(REPO_ROOT)
    except ValueError:
        # The formatter also supports ad-hoc source files outside the checkout;
        # repository generated-directory policy does not apply to those paths.
        return False
    return is_generated_dir(relative)


def collect_sources(paths: list[Path]) -> list[Path]:
    sources: list[Path] = []
    for path in paths:
        if path.is_dir():
            sources.extend(
                sorted(
                    candidate
                    for candidate in path.rglob("*.rz")
                    if candidate.is_file() and not _is_generated_source(candidate)
                )
            )
        else:
            sources.append(path)
    return sources


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--check", action="store_true", help="report files that are not formatted")
    args = parser.parse_args()

    changed = False
    for path in collect_sources(args.paths):
        original = path.read_text(encoding="utf-8")
        formatted = format_text(original)
        if original == formatted:
            continue

        changed = True
        if args.check:
            print(f"needs formatting: {path}", file=sys.stderr)
        else:
            path.write_text(formatted, encoding="utf-8")
            print(f"formatted: {path}")

    return 1 if args.check and changed else 0


if __name__ == "__main__":
    raise SystemExit(main())
