#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
NDK_BUILD=${NDK_BUILD:-"$HOME/Android/Sdk/ndk/23.2.8568313/ndk-build"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

declare -a LABELS=()
declare -a STATUSES=()
declare -a DURATIONS=()
OVERALL=0

record() {
    LABELS+=("$1")
    STATUSES+=("$2")
    DURATIONS+=("$3")
    [ "$2" = PASS ] || OVERALL=1
}

run_step() {
    local label=$1
    shift
    local start end elapsed output status
    start=$(date +%s)
    output="$TMP/${#LABELS[@]}.log"
    if "$@" >"$output" 2>&1; then
        status=0
    else
        status=$?
    fi
    end=$(date +%s)
    elapsed=$((end - start))
    if [ "$status" -ne 0 ]; then
        cat "$output" >&2
        record "$label" FAIL "$elapsed"
        return 0
    fi
    record "$label" PASS "$elapsed"
}

run_ndk() {
    local label=$1
    local directory=$2
    local output="$TMP/ndk-${#LABELS[@]}.log"
    local start end elapsed status
    start=$(date +%s)
    if (cd "$directory" && "$NDK_BUILD" -B -j4) >"$output" 2>&1; then
        status=0
    else
        status=$?
    fi
    end=$(date +%s)
    elapsed=$((end - start))
    if [ "$status" -eq 0 ] &&
        grep -E 'warning:' "$output" |
            grep -vF "argument unused during compilation: '-static-libstdc++'" |
            grep -q .; then
        status=1
        echo "non-benign compiler warning:" >&2
    fi
    if [ "$status" -ne 0 ]; then
        cat "$output" >&2
        record "$label" FAIL "$elapsed"
    else
        record "$label" PASS "$elapsed"
    fi
}

echo "verify_all: $ROOT"

if [ ! -x "$NDK_BUILD" ]; then
    record "ndk-build loader" FAIL 0
    echo "missing executable: $NDK_BUILD" >&2
else
    run_ndk "ndk-build loader" "$ROOT"
    while IFS= read -r makefile; do
        mod_dir=$(dirname "$(dirname "$makefile")")
        run_ndk "ndk-build ${mod_dir#"$ROOT"/}" "$mod_dir"
    done < <(find "$ROOT/mods" -mindepth 3 -maxdepth 3 -type f -path '*/jni/Android.mk' -print | sort)
fi

if [ -f "$ROOT/test/selftest_harness.cpp" ]; then
    run_step "host selftest harness" bash -c '
        cd "$1/test"
        g++ -std=c++17 -Wall -Wextra -Werror -I../jni selftest_harness.cpp -o "$2/selftest_harness"
        "$2/selftest_harness" | tee "$2/selftest.out"
        grep -Fxq "== Resultado: TODOS PASSARAM (0 falhas) ==" "$2/selftest.out"
    ' bash "$ROOT" "$TMP"
else
    record "host selftest harness" FAIL 0
    echo "missing test/selftest_harness.cpp" >&2
fi

run_step "sepolicy grammar" bash -c '
    cd "$1"
    tools/check_sepolicy_rule.sh module/sepolicy.rule
' bash "$ROOT"

while IFS= read -r script; do
    run_step "sh -n ${script#"$ROOT"/}" sh -n "$script"
done < <(find "$ROOT/module" -maxdepth 1 -type f -name '*.sh' -print | sort)

while IFS= read -r script; do
    run_step "bash -n ${script#"$ROOT"/}" bash -n "$script"
done < <(find "$ROOT/tools" -maxdepth 1 -type f -name '*.sh' -print | sort)

if command -v shellcheck >/dev/null 2>&1; then
    while IFS= read -r script; do
        run_step "shellcheck ${script#"$ROOT"/}" shellcheck "$script"
    done < <(find "$ROOT/module" "$ROOT/tools" -maxdepth 1 -type f -name '*.sh' -print | sort)
else
    echo "AVISO: shellcheck não instalado; etapa ignorada"
fi

if [ -f "$ROOT/manager/build.sh" ]; then
    manager_command=
    if grep -Eq 'TestRunner|test' "$ROOT/manager/build.sh"; then
        manager_command="$ROOT/manager/build.sh test"
    elif [ -f "$ROOT/manager/TestRunner" ]; then
        manager_command="$ROOT/manager/TestRunner"
    fi
    if [ -n "$manager_command" ]; then
        run_step "manager JVM tests" bash -c "$manager_command"
    else
        record "manager JVM tests" FAIL 0
        echo "manager/build.sh exists but no test command was discoverable" >&2
    fi
else
    echo "AVISO: manager/build.sh ausente; testes JVM ignorados"
fi

if [ -d "$ROOT/mods/u_patch" ]; then
    if [ -f "$ROOT/mods/u_patch/test/u_patch_arm64.h" ] ||
        find "$ROOT/mods/u_patch" -type f -iname '*arm64*' -print -quit | grep -q .; then
        run_step "u_patch arm64 encoding" bash -c '
            test -x "$1" || exit 1
            makefile=$(find "$2/mods/u_patch" -type f -name Android.mk -print -quit)
            test -n "$makefile"
            (cd "$(dirname "$(dirname "$makefile")")" && "$1" -B -j4)
        ' bash "$NDK_BUILD" "$ROOT"
    else
        record "u_patch arm64 encoding" FAIL 0
        echo "u_patch exists but no arm64 harness was discoverable" >&2
    fi
else
    echo "AVISO: mods/u_patch ausente; encoding arm64 ignorado"
fi

printf '\n| Etapa | Resultado | Tempo (s) |\n|---|---:|---:|\n'
for ((i = 0; i < ${#LABELS[@]}; i++)); do
    printf '| %s | %s | %s |\n' "${LABELS[i]}" "${STATUSES[i]}" "${DURATIONS[i]}"
done
if [ "$OVERALL" -ne 0 ]; then
    echo "verify_all: FAIL"
    exit 1
fi
echo "verify_all: PASS"
