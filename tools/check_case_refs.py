#!/usr/bin/env python3
"""Check test-harness case definitions, duplicate IDs, and citations.

Only .c, .cpp, .h, .hpp, .py, and .java harness files define cases. Markdown,
text, shell scripts, and other scanned files can cite cases but never define them.
"""

from dataclasses import dataclass
import re
import subprocess
import sys
from pathlib import Path

SCAN_SUFFIXES = (".md", ".txt", ".h", ".hpp", ".cpp", ".c", ".py", ".sh", ".java")
DEFINITION_SUFFIXES = (".c", ".cpp", ".h", ".hpp", ".py", ".java")
HARNESS = re.compile(r"(^|/)test/|(harness|_test|Test)\.(c|cpp|h|hpp|py|java)$")
CASE_LABEL = re.compile(r"\[Caso (\d+)\]")
CASE_REFERENCE = re.compile(r"\bCaso (\d+)\b")
OUTPUT_CALL = re.compile(
    r"(?<![A-Za-z0-9_])(?:printf|fprintf|vfprintf|snprintf|puts|"
    r"System\.out\.(?:print|println|printf))\s*\("
)
RAW_STRING_START = re.compile(r"(?:u8|u|U|L)?R\"")
LEADING_SPACE = re.compile(r"(?:\\r\\n|\\n|[ \t\r\n])*")


@dataclass(frozen=True)
class StringLiteral:
    value: str
    start: int
    content_start: int
    end: int


@dataclass(frozen=True)
class Definition:
    path: str
    case_id: int
    line: int
    reference_start: int


@dataclass
class Analysis:
    definitions: list[Definition]
    citations: int
    errors: list[str]


def _skip_comment(source: str, start: int) -> int | None:
    if source.startswith("//", start):
        end = source.find("\n", start + 2)
        return len(source) if end == -1 else end
    if source.startswith("/*", start):
        end = source.find("*/", start + 2)
        return len(source) if end == -1 else end + 2
    return None


def _read_literal(source: str, start: int) -> StringLiteral | None:
    raw_start = RAW_STRING_START.match(source, start)
    if raw_start and (start == 0 or not (source[start - 1].isalnum() or source[start - 1] == "_")):
        delimiter_start = raw_start.end()
        opening = source.find("(", delimiter_start, delimiter_start + 17)
        if opening != -1:
            delimiter = source[delimiter_start:opening]
            closing_token = ")" + delimiter + '"'
            closing = source.find(closing_token, opening + 1)
            if closing != -1:
                return StringLiteral(
                    source[opening + 1:closing], start, opening + 1,
                    closing + len(closing_token)
                )

    if source.startswith('"""', start):
        closing = source.find('"""', start + 3)
        if closing != -1:
            return StringLiteral(source[start + 3:closing], start, start + 3, closing + 3)
        return StringLiteral(source[start + 3:], start, start + 3, len(source))

    if start >= len(source) or source[start] != '"':
        return None
    index = start + 1
    while index < len(source):
        if source[index] == "\\":
            index += 2
        elif source[index] == '"':
            return StringLiteral(source[start + 1:index], start, start + 1, index + 1)
        else:
            index += 1
    return StringLiteral(source[start + 1:], start, start + 1, len(source))


def _skip_character(source: str, start: int) -> int:
    index = start + 1
    while index < len(source):
        if source[index] == "\\":
            index += 2
        elif source[index] == "'":
            return index + 1
        else:
            index += 1
    return len(source)


def _first_literal_in_call(source: str, opening: int) -> StringLiteral | None:
    depth = 1
    index = opening + 1
    first_literal = None
    while index < len(source) and depth:
        comment_end = _skip_comment(source, index)
        if comment_end is not None:
            index = comment_end
            continue

        literal = _read_literal(source, index)
        if literal is not None:
            if first_literal is None:
                first_literal = literal
            index = literal.end
            continue
        if source[index] == "'":
            index = _skip_character(source, index)
            continue
        if source[index] == "(":
            depth += 1
        elif source[index] == ")":
            depth -= 1
        index += 1
    return first_literal


def _output_literals(source: str) -> list[tuple[int, StringLiteral | None]]:
    found = []
    index = 0
    while index < len(source):
        comment_end = _skip_comment(source, index)
        if comment_end is not None:
            index = comment_end
            continue

        literal = _read_literal(source, index)
        if literal is not None:
            index = literal.end
            continue
        if source[index] == "'":
            index = _skip_character(source, index)
            continue

        call = OUTPUT_CALL.match(source, index)
        if call:
            found.append((call.start(), _first_literal_in_call(source, call.end() - 1)))
            index = call.end()
        else:
            index += 1
    return found


def _first_case_label(literal: StringLiteral) -> tuple[int, int] | None:
    leading = LEADING_SPACE.match(literal.value)
    start = leading.end() if leading else 0
    match = CASE_LABEL.match(literal.value, start)
    if not match:
        return None
    return int(match.group(1)), literal.content_start + match.start() + 1


def analyze_sources(sources: dict[str, str]) -> Analysis:
    definitions = []
    definition_starts: dict[str, set[int]] = {}
    errors = []

    for path, source in sorted(sources.items()):
        if Path(path).suffix not in DEFINITION_SUFFIXES or not HARNESS.search(path):
            continue
        for call_start, literal in _output_literals(source):
            if literal is None:
                continue
            if source.count("\n", 0, call_start) != source.count("\n", 0, literal.start):
                continue
            label = _first_case_label(literal)
            if label is None:
                continue
            case_id, reference_start = label
            line = source.count("\n", 0, reference_start) + 1
            # An error output that starts with a case label still defines it;
            # the unique-ID check rejects it if that case already exists.
            definitions.append(Definition(path, case_id, line, reference_start))
            definition_starts.setdefault(path, set()).add(reference_start)

    if not definitions:
        errors.append("nenhum rótulo de caso definido por harness de teste")

    definitions_by_id: dict[int, list[Definition]] = {}
    for definition in definitions:
        definitions_by_id.setdefault(definition.case_id, []).append(definition)
    for case_id, matches in sorted(definitions_by_id.items()):
        if len(matches) > 1:
            locations = ", ".join(f"{item.path}:{item.line}" for item in matches)
            errors.append(f"ID de caso duplicado {case_id}: {locations}")

    defined_ids = set(definitions_by_id)
    citations = 0
    for path, source in sorted(sources.items()):
        for match in CASE_REFERENCE.finditer(source):
            if match.start() in definition_starts.get(path, set()):
                continue
            citations += 1
            case_id = int(match.group(1))
            if case_id not in defined_ids:
                line = source.count("\n", 0, match.start()) + 1
                errors.append(f"{path}:{line}: cita um caso inexistente: {match.group(0)}")

    return Analysis(definitions, citations, errors)


def load_sources(root: Path) -> dict[str, str]:
    tracked = subprocess.check_output(
        ["git", "-C", str(root), "ls-files", "-z"], text=False
    ).decode("utf-8").split("\0")
    return {
        path: (root / path).read_text(encoding="utf-8", errors="replace")
        for path in tracked
        if path and path.endswith(SCAN_SUFFIXES)
    }


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    result = analyze_sources(load_sources(root))
    if result.errors:
        print("\n".join(result.errors), file=sys.stderr)
        return 1
    definition_count = len(result.definitions)
    definition_word = "definição" if definition_count == 1 else "definições"
    uniqueness = "única" if definition_count == 1 else "únicas"
    print(
        f"case-refs: {result.citations} citações verificadas; "
        f"{definition_count} {definition_word} {uniqueness} em harness"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
