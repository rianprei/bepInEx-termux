#!/usr/bin/env python3
"""Regression tests for the case-reference gate."""

import subprocess
import tempfile
from pathlib import Path

CHECKER = Path(__file__).resolve().parents[1] / "tools" / "check_case_refs.py"


def label(case_id: int) -> str:
    return "[" + "Caso " + str(case_id) + "]"


def reference(case_id: int) -> str:
    return "Caso " + str(case_id)


def baseline(case_id: int = 1) -> dict[str, str]:
    return {
        "test/selftest_harness.cpp":
            'int main() { printf("\\n' + label(case_id) + ' baseline"); }\n',
    }


def run_checker(sources: dict[str, str]) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="case-refs-test-") as temporary:
        root = Path(temporary)
        for path, content in sources.items():
            destination = root / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(content, encoding="utf-8")
        subprocess.run(["git", "init", "--quiet"], cwd=root, check=True)
        subprocess.run(["git", "add", "."], cwd=root, check=True)
        return subprocess.run(
            ["python3", str(CHECKER), str(root)],
            capture_output=True,
            check=False,
            text=True,
        )


def expect_failure(name: str, sources: dict[str, str], expected: str) -> None:
    result = run_checker(sources)
    if result.returncode != 1 or expected not in result.stderr:
        raise AssertionError(
            f"{name}: exit={result.returncode}; stdout={result.stdout!r}; "
            f"stderr={result.stderr!r}"
        )
    print(f"  [OK] {name}: exit=1; {expected}")


def expect_pass(name: str, sources: dict[str, str], expected: str) -> None:
    result = run_checker(sources)
    if result.returncode != 0 or expected not in result.stdout:
        raise AssertionError(
            f"{name}: exit={result.returncode}; stdout={result.stdout!r}; "
            f"stderr={result.stderr!r}"
        )
    print(f"  [OK] {name}: exit=0; {expected}")


def run() -> None:
    cases = (
        ('printf("ver ' + label(9999) + ' aqui");', "quoted context"),
        ('printf("erro: ' + label(9999) + '");', "error prefix"),
        ('printf("%s", "' + label(9999) + '");', "second literal"),
    )
    for injection, name in cases:
        sources = baseline()
        sources["test/selftest_harness.cpp"] += injection + "\n"
        expect_failure(name, sources, "cita um caso inexistente")

    author_sabotages = (
        ("test/fuzz/README.md", label(9999)),
        ("test/device/thunk_exec/README.md", label(9999)),
        ("manager/test/io/github/rianprei/bepinex/manager/test/BmodInstallerTest.java",
         label(9999)),
        ("test/selftest_harness.cpp", "// " + label(9999) + " citado"),
        ("docs/ROADMAP-UNIVERSAL.md", label(9999)),
        ("docs/ROADMAP-UNIVERSAL.md", reference(9999)),
    )
    for path, citation in author_sabotages:
        sources = baseline()
        sources[path] = citation + "\n"
        expect_failure(f"author citation sabotage in {path}", sources,
                       "cita um caso inexistente")

    missing_definition = baseline()
    missing_definition["mods/u_patch/jni/u_patch_resolve.h"] = (
        "\n" * 90 + "// citation: " + reference(87) + "\n"
    )
    expect_failure("removed case 87 with live header citation", missing_definition,
                   "mods/u_patch/jni/u_patch_resolve.h:91: cita um caso inexistente")

    duplicate = baseline(39)
    duplicate["test/selftest_harness.cpp"] += (
        'printf("' + label(39) + ' segundo printf");\n'
    )
    expect_failure("second output label is duplicate", duplicate, "ID de caso duplicado 39")

    error_prefix = baseline(39)
    error_prefix["test/selftest_harness.cpp"] += (
        'fprintf(stderr, "' + label(39) + ' erro");\n'
    )
    expect_failure("error output starting with label is duplicate", error_prefix,
                   "ID de caso duplicado 39")

    multiline = baseline()
    multiline["test/selftest_harness.cpp"] += (
        'printf(\n  "' + label(9999) + ' multi-linha"\n);\n'
    )
    expect_failure("multiline output label stays a citation", multiline,
                   "cita um caso inexistente")

    expect_pass(
        "initial newline and spaces identify the first literal",
        {"test/selftest_harness.cpp":
            'int main() { printf("\\n  ' + label(1) + ' baseline"); }\n'},
        "1 definição única",
    )


if __name__ == "__main__":
    run()
