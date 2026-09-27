#!/usr/bin/env bash
# shellcheck disable=SC2016
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
NDK_BUILD=${NDK_BUILD:-"$HOME/Android/Sdk/ndk/23.2.8568313/ndk-build"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
TIMEOUT_BUILD=${TIMEOUT_BUILD:-600}
TIMEOUT_TEST=${TIMEOUT_TEST:-120}
# Device sims forkam muito (adb/su falsos) e rodam na carga junto com os builds
# dos outros agentes: o timeout aqui existe para pegar TRAVA (deadlock, hook
# esperando pra sempre), NÃO lentidão — por isso é bem maior que TIMEOUT_TEST.
TIMEOUT_DEVICE_SIM=${TIMEOUT_DEVICE_SIM:-300}

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
    # Varre todo .cpp do worktree (test/, mods/, selftest e demais raízes);
    # só .git é podado, pois contém objetos/metadados, não arquivos-fonte.
    labels=$(find . -path "./.git" -prune -o -type f -name "*.cpp" \
        -exec grep -h -o -E "\[Caso [0-9]+\]" {} + || true)
    [ -n "$labels" ] || {
        echo "nenhum rótulo [Caso N] encontrado nos diretórios de teste" >&2
        exit 1
    }
    duplicates=$(printf "%s\n" "$labels" | sort | uniq -d || true)
    if [ -n "$duplicates" ]; then
        printf "IDs de caso duplicados: %s\n" "$duplicates" >&2
        exit 1
    fi
' bash "$ROOT"

DOC_REF_COUNT="$TMP/docs-reference-count"
run_step "docs: referencias arquivo:linha" "$TIMEOUT_TEST" bash -c '
    python3 - "$1" "$2" <<"PY"
import re
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
count_file = Path(sys.argv[2])
ref_re = re.compile(r"(?P<path>(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+\.[A-Za-z_][A-Za-z0-9_]*):(?P<start>[0-9]+)(?:-(?P<end>[0-9]+))?")
files = subprocess.check_output(
    ["git", "ls-files", "docs/*.md", "mods/*/README.md"], cwd=root, text=True
).splitlines()
tracked = subprocess.check_output(["git", "ls-files"], cwd=root, text=True).splitlines()
by_name = {}
for item in tracked:
    by_name.setdefault(Path(item).name, []).append(item)
errors = []
checked = 0
for doc_name in files:
    doc = root / doc_name
    for line_no, line in enumerate(doc.read_text(encoding="utf-8").splitlines(), 1):
        refs = list(ref_re.finditer(line))
        if not refs:
            continue
        targets = []
        for match in refs:
            name = match.group("path")
            start = int(match.group("start"))
            end = int(match.group("end") or start)
            candidate = root / name
            if candidate.is_file():
                target = name
            elif "/" not in name and len(by_name.get(name, [])) == 1:
                target = by_name[name][0]
            else:
                errors.append(f"{doc_name}:{line_no}: alvo inexistente/ambiguo: {name}")
                continue
            target_lines = (root / target).read_text(encoding="utf-8", errors="replace").splitlines()
            if start < 1 or end < start or end > len(target_lines):
                errors.append(f"{doc_name}:{line_no}: linha fora do arquivo: {name}:{start}-{end}")
                continue
            targets.append((target, target_lines))
            checked += 1
        literals = [
            span for span in re.findall(r"`([^`\n]+)`", line)
            if not ref_re.fullmatch(span) and not ref_re.search(span)
        ]
        for literal in literals:
            if len(literal) < 4 or "/" in literal and Path(literal).suffix:
                continue
            if targets and not any(literal in "\n".join(lines) for _, lines in targets):
                errors.append(f"{doc_name}:{line_no}: literal nao encontrado: {literal!r}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
count_file.write_text(f"docs: referencias arquivo:linha verificadas: {checked}\n", encoding="utf-8")
PY
' bash "$ROOT" "$DOC_REF_COUNT"
if [ -f "$DOC_REF_COUNT" ]; then
    cat "$DOC_REF_COUNT"
fi

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

for device_script in restore-sim.sh quoting-check.sh device-round2-host-test.sh; do
    path="$ROOT/test/device/$device_script"
    if [ -f "$path" ]; then
        # TIMEOUT_DEVICE_SIM (não TIMEOUT_TEST): pega TRAVA, não lentidão —
        # ver o comentário na definição da variável.
        run_step "device test test/device/$device_script" "$TIMEOUT_DEVICE_SIM" bash "$path"
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

# APK instalável: o que o Android 11+ exige de um APK assinado. Sai depois
# do build do Manager acima. Enquanto o build.sh normalizar resources.arsc
# com DEFLATE, esta etapa FALHA de propósito — é ela que pega o bug do
# INSTALL_PARSE_FAILED (-124) que o host não enxerga. O fix (arsc STORED +
# zipalign depois do link) é do d682d9b na uni/device-run; com ele mergeado
# a etapa passa sem ninguém mexer aqui.
if [ -f "$ROOT/manager/build.sh" ] && [ -f "$ROOT/manager/bepinex-manager.apk" ] \
   && [ -x "$ROOT/tools/check_apk_installable.sh" ]; then
    run_step "APK instalavel" "$TIMEOUT_TEST" bash -c '
        cd "$1"
        want=$(awk "{print \$2}" VERSION)
        bash tools/check_apk_installable.sh manager/bepinex-manager.apk "$want"
    ' bash "$ROOT"
else
    record "APK instalavel (not present)" SKIP 0 0
    echo "AVISO: tools/check_apk_installable.sh ou o APK do Manager ausente" >&2
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

# Fuzzing com sanitizers dos 4 parsers que recebem DADO DO USUÁRIO dentro do
# processo do jogo: linhas .bpatch/.conf do u_patch, o preflight de ELF (com a
# guarda de SONAME do frida-gadget), o validador do config do frida e o resto
# da superfície de string do selftest. Um crash de parser aqui derruba o jogo,
# e 2 mortes em 20sShut ele inteiro pelo crashguard.
#
# A etapa é curta e DETERMINÍSTICA (seed fixa, contagem de execs fixa, ~25s):
# ela não substitui as rodadas longas de 10 min por alvo, que são o trabalho
# de achar bug (test/fuzz/README.md) — ela é o PISO, e o piso nunca é SKIP:
# clang ou sanitizer ausente é FAIL, porque um gate que pula o fuzzing quando
# o host não tem toolchain volta a ser "PASS" sem exercitar parser nenhum.
if [ -f "$ROOT/test/fuzz/run_fuzz_gate.sh" ]; then
    # TIMEOUT_FUZZ, e não TIMEOUT_TEST: o limite aqui é o de 4 alvos com
    # sanitizer (build + execs), não o de um binário de teste.
    run_step "fuzz parsers (ASan+UBSan, seed fixa)" "${TIMEOUT_FUZZ:-300}" \
        bash "$ROOT/test/fuzz/run_fuzz_gate.sh"
else
    record "fuzz parsers (run_fuzz_gate.sh ausente)" FAIL 0 1
    echo "test/fuzz/run_fuzz_gate.sh ausente: os parsers de dado do usuario ficam sem cobertura no gate"
fi

# Execução real do thunk (qemu-aarch64): run_host.sh do thunk_exec.
# qemu ausente = SKIP com aviso, nunca PASS.
if [ -f "$ROOT/test/device/thunk_exec/run_host.sh" ]; then
    if [ -x "${QEMU:-/usr/bin/qemu-aarch64}" ] || command -v qemu-aarch64 >/dev/null 2>&1; then
        run_step "u_patch exec test (thunk_exec)" 420 bash "$ROOT/test/device/thunk_exec/run_host.sh"
    else
        record "u_patch exec test (thunk_exec, qemu-aarch64 missing)" SKIP 0 0
        echo "AVISO: qemu-aarch64 ausente; exec test do u_patch ignorado" >&2
    fi
else
    record "u_patch exec test (not present)" SKIP 0 0
fi

# dll-coverage: MANIFEST.tsv ↔ DLL-COVERAGE.md ↔ README
# O MANIFEST.tsv é a fonte da verdade do corpus; o doc e o README têm que
# bater com ele. Apagar 1 linha do MANIFEST → gate FALHA.
if [ -f "$ROOT/MANIFEST.tsv" ] && [ -f "$ROOT/docs/DLL-COVERAGE.md" ]; then
    run_step "dll-coverage MANIFEST ↔ doc ↔ README" "$TIMEOUT_TEST" bash -c '
        manifest="$1/MANIFEST.tsv"
        doc="$1/docs/DLL-COVERAGE.md"
        readme="$1/README.md"

        # Conta IDs únicos do MANIFEST (1ª coluna)
        manifest_count=$(cut -f1 "$manifest" | sort -u | wc -l | tr -d " ")
        [ "$manifest_count" -gt 0 ] || { echo "MANIFEST.tsv vazio ou ilegível"; exit 1; }

        # Extrai "Total de mods no corpus" do doc
        doc_total=$(grep -oP "Total de mods no corpus \| \K[0-9]+" "$doc" || true)
        [ -n "$doc_total" ] || { echo "DLL-COVERAGE.md: sem Total de mods"; exit 1; }

        # Extrai "N mods reais" do README
        readme_mods=$(grep -oP "medido em \K[0-9]+ mods reais" "$readme" | grep -oP "^[0-9]+" || true)
        [ -n "$readme_mods" ] || { echo "README: sem contagem de mods"; exit 1; }

        # Verifica 375+3=378 e 0/378 entre doc e README
        doc_refusals=$(grep -oP "Recusas .* \| \K[0-9]+" "$doc" | head -1 | tr -d " " || true)
        doc_nested=$(grep -oP "classe aninhada.* \| \K[0-9]+" "$doc" | head -1 | tr -d " " || true)
        doc_total_patches=$(grep -oP "Total de patches Harmony \| \K[0-9]+" "$doc" | head -1 | tr -d " " || true)
        doc_translated=$(grep -oP "\*\*\K0(?=/)" "$doc" | head -1 | tr -d " " || true)
        # Extrai "0 de N patches" do README — N tem que bater com doc_total_patches
        readme_patches_line=$(grep -oP "\K0 de [0-9]+ patches" "$readme" | head -1 || true)
        readme_translated=$(echo "$readme_patches_line" | grep -oP "^0" || true)
        readme_total_patches=$(echo "$readme_patches_line" | grep -oP "de \K[0-9]+" | head -1 || true)

        [ "$manifest_count" = "$doc_total" ] || { echo "MANIFEST ($manifest_count) != doc Total ($doc_total)"; exit 1; }
        [ "$manifest_count" = "$readme_mods" ] || { echo "MANIFEST ($manifest_count) != README mods ($readme_mods)"; exit 1; }
        [ "$((doc_refusals + doc_nested))" = "$doc_total_patches" ] || { echo "doc: $doc_refusals + $doc_nested != $doc_total_patches"; exit 1; }
        [ -n "$readme_total_patches" ] || { echo "README: sem 0 de N patches"; exit 1; }
        [ "$readme_total_patches" = "$doc_total_patches" ] || { echo "README patches ($readme_total_patches) != doc ($doc_total_patches)"; exit 1; }
        [ "$readme_translated" = "$doc_translated" ] || { echo "README traduzidos ($readme_translated) != doc ($doc_translated)"; exit 1; }
        echo "MANIFEST=$manifest_count doc=$doc_total readme=$readme_mods patches=$doc_total_patches refusals=$doc_refusals nested=$doc_nested translated=$doc_translated"
    ' bash "$ROOT"
else
    record "dll-coverage MANIFEST ↔ doc ↔ README" FAIL 0 1
    echo "MANIFEST.tsv ou docs/DLL-COVERAGE.md ausente" >&2
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
