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

run_step "outputs ARM64/ARM32 do loader e mods" "$TIMEOUT_TEST" python3 - "$ROOT" <<'PY'
import struct
import sys
from pathlib import Path

root = Path(sys.argv[1])
artifacts = [
    ("libs", "libbc-poc.so"),
    ("mods/u_patch/libs", "libu_patch.so"),
    ("mods/u_noads/libs", "libu_noads.so"),
    ("mods/u_dump/libs", "libu_dump.so"),
    ("mods/u_frida/libs", "libu_frida.so"),
    ("mods/sa2ammo/libs", "libsa2ammo.so"),
    ("mods/sa2content/libs", "libsa2content.so"),
]
for directory, filename in artifacts:
    for abi, elf_class, machine in (("arm64-v8a", 2, 183), ("armeabi-v7a", 1, 40)):
        path = root / directory / abi / filename
        if not path.is_file():
            raise SystemExit(f"artefato ausente: {path.relative_to(root)}")
        head = path.read_bytes()[:20]
        got_machine = struct.unpack_from("<H", head, 18)[0] if len(head) >= 20 else -1
        if head[:4] != b"\x7fELF" or head[4] != elf_class or head[5] != 1 or got_machine != machine:
            raise SystemExit(f"arquitetura errada em {path.relative_to(root)}")
        if abi == "armeabi-v7a" and filename in ("libu_patch.so", "libu_noads.so"):
            if "não suportado em 32-bit".encode() not in path.read_bytes():
                raise SystemExit(f"aviso de recurso indisponível ausente em {path.relative_to(root)}")
print("loader, mods universais e mods SA2 têm ELF ARM64 e ARM32 corretos")
print("u_patch/u_noads ARM32 contêm recusa explícita de hooks AArch64")
PY

if [ -x "$NDK_BUILD" ]; then
    run_step "zip Magisk contém loader ARM64 e ARM32" "$TIMEOUT_BUILD" bash -c '
        set -e
        out="$1/module"
        NDK="$(dirname "$3")" OUT_DIR="$out" "$2/tools/build_module.sh"
        bash "$2/test/module_zip_abi_check.sh" "$out/bepinex-termux-$(awk "{print \$1}" "$2/VERSION").zip"
    ' bash "$TMP" "$ROOT" "$NDK_BUILD"
else
    record "zip Magisk contém loader ARM64 e ARM32" SKIP 0 0
    echo "missing executable: $NDK_BUILD" >&2
fi

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
done < <(find "$ROOT/test" -maxdepth 1 -type f \
    \( -name '*_test.cpp' -o -name 'selftest_harness.cpp' \) \
    ! -name 'mono_min_test.cpp' -print | sort)
if [ ! -f "$ROOT/test/selftest_harness.cpp" ]; then
    record "host selftest_harness.cpp" FAIL 0 1
    echo "selftest_harness.cpp ausente: teste central do loader" >&2
fi

run_step "host test/mono_min_test.cpp" "$TIMEOUT_TEST" bash -c '
    set -e
    tmp="$1"
    root="$2"
    g++ -std=c++17 -Wall -Wextra -Werror -fPIC -shared \
        "$root/test/fixtures/mono_min_api_complete.cpp" -o "$tmp/mono_min_api_complete.so"
    g++ -std=c++17 -Wall -Wextra -Werror -fPIC -shared \
        "$root/test/fixtures/mono_min_api_missing.cpp" -o "$tmp/mono_min_api_missing.so"
    g++ -std=c++17 -Wall -Wextra -Werror "$root/test/mono_min_test.cpp" \
        -ldl -o "$tmp/mono_min_test"
    "$tmp/mono_min_test" "$tmp/mono_min_api_complete.so" "$tmp/mono_min_api_missing.so"
' bash "$TMP" "$ROOT"

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
# UX-REFERENCE citations must each carry one exact source anchor. The anchor
# must occur once in its target file, inside the cited range, and on code rather
# than package/import/comment/license lines. This proves the quote can be found,
# not that it semantically supports the prose; that remains a review judgment.
run_step "docs: referencias arquivo:linha" "$TIMEOUT_TEST" bash -c '
    python3 - "$1" "$2" <<"PY"
import re
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
count_file = Path(sys.argv[2])
ref_re = re.compile(r"(?P<path>(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+\.[A-Za-z_][A-Za-z0-9_]*):(?P<start>[0-9]+)(?:-(?P<end>[0-9]+))?")
anchor_re = re.compile(r"\(anchor:\s*`([^`]+)`\)")
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
        is_ux_reference = doc_name == "docs/UX-REFERENCE.md"
        anchors = anchor_re.findall(line) if is_ux_reference else []
        if is_ux_reference and len(anchors) != len(refs):
            errors.append(
                f"{doc_name}:{line_no}: cada citação exige uma âncora explícita "
                "no formato (anchor: `texto`)"
            )
        targets = []
        for ref_index, match in enumerate(refs):
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
            targets.append((target, target_lines, start, end))
            checked += 1
            if is_ux_reference and ref_index < len(anchors):
                anchor = anchors[ref_index]
                occurrence_count = sum(source_line.count(anchor) for source_line in target_lines)
                occurrences = [
                    (idx, source_line) for idx, source_line in enumerate(target_lines)
                    if anchor in source_line
                ]
                if occurrence_count != 1:
                    errors.append(
                        f"{doc_name}:{line_no}: âncora {anchor!r} ocorre "
                        f"{occurrence_count} vezes em {target}; esperado exatamente uma"
                    )
                    continue
                anchor_line, source_line = occurrences[0]
                if not start - 1 <= anchor_line <= end - 1:
                    errors.append(
                        f"{doc_name}:{line_no}: âncora {anchor!r} fora da faixa "
                        f"citada {name}:{start}-{end}"
                    )
                stripped = source_line.lstrip()
                if re.match(r"(?:package|import)\b", stripped):
                    errors.append(
                        f"{doc_name}:{line_no}: âncora {anchor!r} está em "
                        f"package/import ({target}:{anchor_line + 1})"
                    )
                    continue
                in_comment = False
                comment_line = False
                for source_index, candidate_line in enumerate(target_lines[:anchor_line + 1]):
                    candidate = candidate_line.lstrip()
                    if source_index == anchor_line and (
                        in_comment
                        or candidate.startswith(("//", "/*", "*", "*/", "<!--", "-->"))
                    ):
                        comment_line = True
                    if in_comment:
                        if "*/" in candidate_line or "-->" in candidate_line:
                            in_comment = False
                    elif candidate.startswith("/*") and "*/" not in candidate_line:
                        in_comment = True
                    elif candidate.startswith("<!--") and "-->" not in candidate_line:
                        in_comment = True
                if comment_line:
                    errors.append(
                        f"{doc_name}:{line_no}: âncora {anchor!r} está em "
                        f"comentário/licença ({target}:{anchor_line + 1})"
                    )
        literals = [
            span for span in re.findall(r"`([^`\n]+)`", line)
            if not ref_re.fullmatch(span) and not ref_re.search(span)
        ]
        for literal in literals:
            if len(literal) < 4 or "/" in literal and Path(literal).suffix:
                continue
            if not is_ux_reference and targets and not any(
                literal in "\n".join(lines) for _, lines, _, _ in targets
            ):
                errors.append(f"{doc_name}:{line_no}: literal nao encontrado na evidencia citada: {literal!r}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
count_file.write_text(f"docs: referencias arquivo:linha verificadas: {checked}\n", encoding="utf-8")
PY
' bash "$ROOT" "$DOC_REF_COUNT"
if [ -f "$DOC_REF_COUNT" ]; then
    cat "$DOC_REF_COUNT"
fi

# Hash citado como mesclado tem que existir e ser ancestral de HEAD. Sem
# isso, um hash de branch fora da base escrito como 'merged' passa no gate
# e a verdade só aparece na revisão humana.
if [ -f "$ROOT/test/docs_hash_gate.sh" ]; then
    run_step "docs: hash mesclado existe na base" "$TIMEOUT_TEST" \
        bash "$ROOT/test/docs_hash_gate.sh"
  else
      record "docs: hash mesclado existe na base (check ausente)" FAIL 0 1
      echo "test/docs_hash_gate.sh ausente: hash fora da base pode posar de mesclado" >&2
  fi

  # A fixture roda o gate de verdade num repo temporario. Sem ela, o nome
  # CHANGELOG.md na lista de cobertura do gate e so estrutural: nenhuma linha
  # do CHANGELOG do repo se declara mesclada com hash, entao nada prova que uma
  # linha ali seria conferida. A fixture e o que prova.
  if [ -f "$ROOT/test/docs_hash_gate_fixture.sh" ]; then
      run_step "docs: fixture do hash-gate" "$TIMEOUT_TEST" \
          bash "$ROOT/test/docs_hash_gate_fixture.sh"
  else
      record "docs: fixture do hash-gate (ausente)" FAIL 0 1
      echo "test/docs_hash_gate_fixture.sh ausente: a cobertura do CHANGELOG fica so estrutural" >&2
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

# Fuzzing com sanitizers dos parsers que recebem DADO DO USUÁRIO dentro do
# processo do jogo (ver TARGETS em test/fuzz/run_fuzz_gate.sh): linhas
# .bpatch/.conf do u_patch, o preflight de ELF (com a guarda de SONAME do
# frida-gadget), o validador do config do frida, o resto da superfície de
# string do selftest e o emissor de thunk arm64 do u_patch. Um crash de
# parser aqui derruba o jogo, e 2 mortes em 20s travam ele inteiro pelo
# crashguard.
#
# A etapa é curta e DETERMINÍSTICA (seed fixa, contagem de execs fixa, ~25s):
# ela não substitui as rodadas longas de 10 min por alvo, que são o trabalho
# de achar bug (test/fuzz/README.md) — ela é o PISO, e o piso nunca é SKIP:
# clang ou sanitizer ausente é FAIL, porque um gate que pula o fuzzing quando
# o host não tem toolchain volta a ser "PASS" sem exercitar parser nenhum.
if [ -f "$ROOT/test/fuzz/run_fuzz_gate.sh" ]; then
    # TIMEOUT_FUZZ, e não TIMEOUT_TEST: o limite aqui é o dos alvos com
    # sanitizer (build + execs), não o de um binário de teste.
    run_step "fuzz parsers (ASan+UBSan, seed fixa)" "${TIMEOUT_FUZZ:-300}" \
        bash "$ROOT/test/fuzz/run_fuzz_gate.sh"
else
    record "fuzz parsers (run_fuzz_gate.sh ausente)" FAIL 0 1
    echo "test/fuzz/run_fuzz_gate.sh ausente: os parsers de dado do usuario ficam sem cobertura no gate"
fi

# Build-id reproduzível: o MESMO commit tem que dar o MESMO build-id em
# diretórios diferentes, senão o build-id de um tombstone não identifica nada
# e um crash de usuário não vira função:linha. Foi o que travou o crash do SA2
# (tombstone_07): o build-id 041d9b51... não batia com nenhum build local.
# A etapa compila em DOIS diretórios temporários de profundidades diferentes e
# exige build-id e .so byte a byte iguais. Diferente = FAIL, nunca SKIP.
if [ -f "$ROOT/test/symbols/build_id_repro_test.sh" ]; then
    run_step "build-id reproduzivel (2 diretorios)" "${TIMEOUT_SYMBOLS:-600}" \
        bash "$ROOT/test/symbols/build_id_repro_test.sh"
else
    record "build-id reproduzivel (test ausente)" FAIL 0 1
    echo "test/symbols/build_id_repro_test.sh ausente: o build-id volta a depender do diretorio"
fi

# Nada que sai da máquina pode carregar símbolo. Com APP_STRIP_MODE := none
# (jni/repro.mk) o .so de build tem ~1,8 MB de DWARF, e TODO consumidor de
# mods/*/libs/arm64-v8a/*.so passa a ter esse binário na mão: o .bmod que o
# usuário baixa, o .so do adb push e o u_dump.so nos assets do APK. A etapa
# gera um mod de verdade (new_mod.sh), empacota em .bmod e confere que o .so
# entregue está stripped e com o build-id preservado. Diferente = FAIL.
if [ -f "$ROOT/test/symbols/ship_stripped_test.sh" ]; then
    run_step "nada entregue leva simbolo" "${TIMEOUT_SYMBOLS:-600}" \
        bash "$ROOT/test/symbols/ship_stripped_test.sh"
else
    record "nada entregue leva simbolo (teste ausente)" FAIL 0 1
    echo "test/symbols/ship_stripped_test.sh ausente: o .so nao-stripado vaza para o .bmod/APK/device"
fi

# Nenhuma rota (doc ou script) entrega .so de libs/ ou obj/ sem strip.
# O teste acima prova o artefato; este prova as ROTAS: adb push / cp pra
# /data com origem no diretório de build, direto ou via variável sem
# symbols_ship no mesmo arquivo.
if [ -f "$ROOT/test/symbols/ship_routes_check.sh" ]; then
    run_step "rotas entregam só .so stripado" "$TIMEOUT_TEST" \
        bash "$ROOT/test/symbols/ship_routes_check.sh"
else
    record "rotas entregam só .so stripado (check ausente)" FAIL 0 1
    echo "test/symbols/ship_routes_check.sh ausente: doc/script pode empurrar .so nao-stripado"
fi

# O build não pode depender de ONDE o NDK está. A raiz do NDK era descoberta por
# um glob em "$HOME/Android/Sdk/ndk/*", que funciona nesta máquina e só nesta:
# com o NDK em /opt, em ANDROID_NDK_HOME, num CI ou no home de outro usuário o
# glob não acha, o prefix-map da raiz do NDK some, e o caminho de máquina volta
# a vazar com o build-id mudando — exatamente o item 3.
# A etapa compila a mesma árvore com o NDK no $HOME e com o NDK apontado para um
# caminho FORA do $HOME, e exige sha256 idêntico do símbolo e do .so entregue.
if [ -f "$ROOT/test/symbols/ndk_path_test.sh" ]; then
    run_step "build independe do caminho do NDK" "${TIMEOUT_SYMBOLS:-600}" \
        bash "$ROOT/test/symbols/ndk_path_test.sh"
else
    record "build independe do caminho do NDK (teste ausente)" FAIL 0 1
    echo "test/symbols/ndk_path_test.sh ausente: o build-id volta a depender de onde o NDK esta"
fi

# tools/symbolize.sh: o crash do usuário tem que virar função:linha em 1
# comando. O teste cruza um tombstone sintético, o tombstone REAL do device e
# o cruzamento histórico do offset 0x1bb34 com o build que o gerou.
if [ -f "$ROOT/test/symbols/symbolize_test.sh" ] && [ -x "$ROOT/tools/symbolize.sh" ]; then
    run_step "symbolize.sh (tombstone -> funcao:linha)" "${TIMEOUT_SYMBOLS:-600}" \
        bash "$ROOT/test/symbols/symbolize_test.sh"
else
    record "symbolize.sh (teste ausente)" FAIL 0 1
    echo "test/symbols/symbolize_test.sh ausente: crash de usuario nao vira funcao:linha"
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

# O TOMBSTONE e entrada de outra pessoa: um crash report que o usuario manda
# nunca pode virar comando na maquina de quem symboliza (revisao do OpenCode em
# 668cc9f: o awk montava uma command line com o token .so e rodava com
# `cmd | getline`).
if [ -f "$ROOT/test/symbols/symbolize_injection_test.sh" ]; then
    run_step "symbolize: tombstone nunca vira comando" "$TIMEOUT_TEST" \
        bash "$ROOT/test/symbols/symbolize_injection_test.sh"
else
    record "symbolize: tombstone nunca vira comando (teste ausente)" FAIL 0 1
    echo "test/symbols/symbolize_injection_test.sh ausente: o tombstone pode executar comando"
fi

# Guarda de arquitetura dos hooks: em ARM32, todo mod que usa DobbyHook tem que
# recusar COM LOG antes de alcancar o hook, porque o hook so foi validado em
# aparelho AArch64 (revisao de 5edfb41: sa2ammo e sa2content chamavam
# DobbyHook sem nenhuma guarda).
if [ -f "$ROOT/test/arm32_hook_guard_check.py" ]; then
    run_step "hooks ARM32 recusam 32-bit" "$TIMEOUT_TEST" \
        python3 "$ROOT/test/arm32_hook_guard_check.py" "$ROOT"
else
    record "hooks ARM32 recusam 32-bit (check ausente)" FAIL 0 1
    echo "test/arm32_hook_guard_check.py ausente: um hook pode rodar em 32-bit sem validacao"
fi

# O cliente do REPL do Termux (achado A4 do wiring-audit): 11 dos 12 verbos do
# companion tm como UNICO sender esse cliente, e o console apontava para um
# arquivo que NINGUEM instalava. O teste sobe um abstract socket (como o
# companion) e fala com ele pelo cliente REAL, verbo por verbo — incluindo o
# multi-linha de list_mods e o keep-alive de stream, que sao os dois jeitos
# classicos de o cliente quebrar.
if [ -f "$ROOT/test/termux_client_test.py" ]; then
    run_step "cliente do REPL do Termux (12 verbos)" "$TIMEOUT_TEST" \
        python3 "$ROOT/test/termux_client_test.py"
else
    record "cliente do REPL do Termux (teste ausente)" FAIL 0 1
    echo "test/termux_client_test.py ausente: o lado que envia o protocolo nao e testado"
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
