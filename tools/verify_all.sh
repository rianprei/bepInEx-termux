#!/usr/bin/env bash
# shellcheck disable=SC2016
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
NDK_BUILD=${NDK_BUILD:-"$HOME/Android/Sdk/ndk/23.2.8568313/ndk-build"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
TIMEOUT_BUILD=${TIMEOUT_BUILD:-600}
TIMEOUT_TEST=${TIMEOUT_TEST:-120}

declare -a LABELS=()
declare -a STATUSES=()
declare -a DURATIONS=()
declare -a EXITS=()
OVERALL=0
SKIPS=0

record() {
    LABELS+=("$1")
    STATUSES+=("$2")
    DURATIONS+=("$3")
    EXITS+=("${4:-0}")
    if [ "$2" = SKIP ]; then
        SKIPS=$((SKIPS + 1))
    elif [ "$2" != PASS ]; then
        OVERALL=1
    fi
}

run_step() {
    local label=$1
    local limit=$2
    shift
    shift
    local start end elapsed output status
    start=$(date +%s)
    output="$TMP/${#LABELS[@]}.log"
    if timeout --foreground "$limit" "$@" >"$output" 2>&1; then
        status=0
    else
        status=$?
    fi
    end=$(date +%s)
    elapsed=$((end - start))
    if [ "$status" -eq 124 ]; then
        echo "timeout after ${limit}s: $label" >&2
    fi
    if [ "$status" -ne 0 ]; then
        cat "$output" >&2
        record "$label" FAIL "$elapsed" "$status"
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
    if timeout --foreground "$TIMEOUT_BUILD" bash -c '
        cd "$1"
        "$2" APP_CFLAGS+="-Wall -Wextra" APP_CPPFLAGS+="-Wall -Wextra" -B -j4
    ' bash "$directory" "$NDK_BUILD" >"$output" 2>&1; then
        status=0
    else
        status=$?
    fi
    end=$(date +%s)
    elapsed=$((end - start))
    if [ "$status" -eq 0 ] && grep -E 'warning:' "$output" | grep -q .; then
        status=1
        echo "non-benign compiler warning:" >&2
    fi
    if [ "$status" -ne 0 ]; then
        cat "$output" >&2
        record "$label" FAIL "$elapsed" "$status"
    else
        record "$label" PASS "$elapsed" "$status"
    fi
}

echo "verify_all: $ROOT"

if [ -x "$NDK_BUILD" ]; then
    run_ndk "ndk-build loader" "$ROOT"
else
    record "ndk-build loader" SKIP 0 0
    echo "missing executable: $NDK_BUILD" >&2
fi
while IFS= read -r mod_dir; do
    [ "$(basename "$mod_dir")" = common ] && continue
    label="ndk-build ${mod_dir#"$ROOT"/}"
    if [ ! -f "$mod_dir/jni/Android.mk" ]; then
        record "$label (missing jni/Android.mk)" SKIP 0 0
        echo "$label: missing jni/Android.mk" >&2
    elif [ -x "$NDK_BUILD" ]; then
        run_ndk "$label" "$mod_dir"
    else
        record "$label" SKIP 0 0
        echo "missing executable: $NDK_BUILD" >&2
    fi
done < <(find "$ROOT/mods" -mindepth 1 -maxdepth 1 -type d -print | sort)

while IFS= read -r makefile; do
    mod_dir=$(dirname "$(dirname "$makefile")")
    case "$mod_dir" in
        "$ROOT"/mods/common|"$ROOT"/mods/*) ;;
        *) record "unexpected Android.mk ${makefile#"$ROOT"/}" FAIL 0 1
           echo "Android.mk is outside mods/<id>/jni/: $makefile" >&2 ;;
    esac
done < <(find "$ROOT/mods" -type f -name Android.mk -print | sort)

while IFS= read -r test_file; do
    test_name=${test_file#"$ROOT"/}
    binary="$TMP/$(basename "$test_file" .cpp)"
    if [[ "$test_file" == */selftest_harness.cpp ]]; then
        run_step "host $test_name" "$TIMEOUT_TEST" bash -o pipefail -c '
            cd "$1"
            g++ -std=c++17 -Wall -Wextra -Werror -I../jni "$2" -o "$3"
            "$3" | tee "$4"
            test "${PIPESTATUS[0]}" -eq 0
            test "$(tail -n 1 "$4")" = "== Resultado: TODOS PASSARAM (0 falhas) =="
        ' bash "$ROOT/test" "$(basename "$test_file")" "$binary" "$TMP/$(basename "$test_file").out"
    else
        run_step "host $test_name" "$TIMEOUT_TEST" bash -c '
            cd "$1"
            g++ -std=c++17 -Wall -Wextra -Werror -I../jni "$2" -o "$3"
            "$3"
        ' bash "$ROOT/test" "$(basename "$test_file")" "$binary"
    fi
done < <(find "$ROOT/test" -maxdepth 1 -type f \( -name '*_test.cpp' -o -name 'selftest_harness.cpp' \) -print | sort)
if [ ! -f "$ROOT/test/selftest_harness.cpp" ]; then
    record "host selftest_harness.cpp" FAIL 0 1
    echo "selftest_harness.cpp ausente: teste central do loader" >&2
fi

while IFS= read -r test_file; do
    test_name=${test_file#"$ROOT"/}
    binary="$TMP/$(basename "$test_file" .cpp)"
    run_step "host $test_name" "$TIMEOUT_TEST" bash -c '
        cd "$(dirname "$1")"
        g++ -std=c++17 -Wall -Wextra -Werror -I jni "$(basename "$1")" -o "$2"
        "$2"
    ' bash "$test_file" "$binary"
done < <(find "$ROOT/mods" -type f \( -name 'test_targets.cpp' -o -name 'test_closers.cpp' \) -print | sort)

run_step "harness case ids unicos" "$TIMEOUT_TEST" bash -c '
    cd "$1"
    duplicates=$(git grep -h -E "\[Caso [0-9]+\]" -- test/selftest_harness.cpp |
        grep -oE "\[Caso [0-9]+\]" | sort | uniq -d || true)
    if [ -n "$duplicates" ]; then
        printf "IDs duplicados: %s\n" "$duplicates" >&2
        exit 1
    fi
' bash "$ROOT"

run_step "sepolicy grammar" "$TIMEOUT_TEST" bash -c '
    cd "$1"
    tools/check_sepolicy_rule.sh module/sepolicy.rule
' bash "$ROOT"

if [ "${VERIFY_RELEASE:-0}" = 1 ]; then
    if [ -x "$ROOT/tools/build_release.sh" ] && [ -x "$NDK_BUILD" ]; then
        run_step "release reproduzivel" 600 bash -c '
            set -e
            tmp=$(mktemp -d)
            trap "rm -rf \"$tmp\"" EXIT
            "$1/tools/build_release.sh" --output "$tmp/one"
            "$1/tools/build_release.sh" --output "$tmp/two"
            cmp "$tmp/one/$(cat "$1/VERSION" | awk "{print \$1}")/SHA256SUMS" \
                "$tmp/two/$(cat "$1/VERSION" | awk "{print \$1}")/SHA256SUMS"
        ' bash "$ROOT"
    else
        record "release reproduzivel" SKIP 0 0
        echo "release reproduzivel: build-tools/ndk ausente; use VERIFY_RELEASE=1 em ambiente completo" >&2
    fi
fi

while IFS= read -r script; do
    rel=${script#"$ROOT"/}
    if head -n 1 "$script" | grep -q bash; then
        run_step "bash -n $rel" "$TIMEOUT_TEST" bash -n "$script"
    else
        run_step "sh -n $rel" "$TIMEOUT_TEST" sh -n "$script"
    fi
done < <(cd "$ROOT" && git ls-files '*.sh' | sort)

if shellcheck_bin=$("$ROOT/tools/fetch_shellcheck.sh"); then
    while IFS= read -r script; do
        label="shellcheck ${script#"$ROOT"/}"
        start=$(date +%s)
        shellcheck_output="$TMP/shellcheck-${#LABELS[@]}.log"
        if timeout --foreground "$TIMEOUT_TEST" "$shellcheck_bin" "$script" >"$shellcheck_output" 2>&1; then
            record "$label" PASS "$(( $(date +%s) - start ))"
        else
            shellcheck_status=$?
            cat "$shellcheck_output" >&2
            echo "AVISO: $label encontrou achados; scripts de outros agentes nao foram alterados" >&2
            record "$label (findings reported)" FAIL "$(( $(date +%s) - start ))" "$shellcheck_status"
        fi
    done < <(cd "$ROOT" && git ls-files '*.sh' | sed "s#^#$ROOT/#" | sort)
else
    record "shellcheck bootstrap" FAIL 0 1
    echo "ShellCheck could not be downloaded or verified" >&2
fi

while IFS= read -r test_script; do
    run_step "shell test ${test_script#"$ROOT"/}" "$TIMEOUT_TEST" bash "$test_script"
done < <(find "$ROOT/test" -maxdepth 1 -type f -name '*_test.sh' -print | sort)

for device_script in restore-sim.sh quoting-check.sh; do
    path="$ROOT/test/device/$device_script"
    if [ -f "$path" ]; then
        run_step "device test test/device/$device_script" "$TIMEOUT_TEST" bash "$path"
    else
        record "device test test/device/$device_script (not present)" SKIP 0 0
    fi
done

if [ -f "$ROOT/manager/build.sh" ]; then
    if [ -x "$ROOT/manager/run_tests.sh" ]; then
        run_step "manager JVM tests" "$TIMEOUT_TEST" bash -c '
            cd "$1/manager"
            ./build.sh
            ./run_tests.sh
        ' bash "$ROOT"
    else
            record "manager JVM tests (missing run_tests.sh)" SKIP 0 0
        echo "manager/build.sh exists but manager/run_tests.sh is missing" >&2
    fi
else
    record "manager JVM tests (not present)" SKIP 0 0
    echo "AVISO: manager/build.sh ausente; testes JVM ignorados"
fi

if [ -d "$ROOT/mods/u_patch" ]; then
    run_step "u_patch encoding harness" "$TIMEOUT_TEST" bash -c '
        cd "$1"
        found=0
        while IFS= read -r test_file; do
            found=1
            g++ -std=c++17 -Wall -Wextra -Werror "$test_file" -o "$2/$(basename "$test_file" .cpp)"
            "$2/$(basename "$test_file" .cpp)"
        done < <(find mods/u_patch -type f \( -name "*harness*.cpp" -o -name "*test*.cpp" \) -print)
        test "$found" -eq 1
    ' bash "$ROOT" "$TMP"
else
    record "u_patch encoding harness (not present)" SKIP 0 0
    echo "AVISO: mods/u_patch ausente; encoding arm64 ignorado"
fi

if [ -f "$ROOT/VERSION" ] && grep -q '^#define BC_LOADER_VERSION ' "$ROOT/jni/main.cpp"; then
    run_step "VERSION matches loader" "$TIMEOUT_TEST" bash -c '
        version=$(awk "{print \$1}" "$1/VERSION")
        loader=$(sed -n "s/^#define BC_LOADER_VERSION \"\\(.*\\)\"/\\1/p" "$1/jni/main.cpp")
        test "$version" = "$loader"
    ' bash "$ROOT"
else
    record "VERSION matches loader" FAIL 0 1
    echo "VERSION or jni/main.cpp version define missing" >&2
fi

printf '\n| Etapa | Resultado | Exit | Tempo (s) |\n|---|---:|---:|---:|\n'
for ((i = 0; i < ${#LABELS[@]}; i++)); do
    printf '| %s | %s | %s | %s |\n' "${LABELS[i]}" "${STATUSES[i]}" "${EXITS[i]}" "${DURATIONS[i]}"
done
echo "verify_all: SKIP=${SKIPS}"
if [ "$OVERALL" -ne 0 ]; then
    echo "verify_all: FAIL"
    exit 1
fi
echo "verify_all: PASS"
