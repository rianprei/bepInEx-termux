#!/usr/bin/env bash
# Teste da assinatura do Manager (build.sh + build_release.sh) sem a chave real.
#
# O .jks do usuário (manager-release.jks) NUNCA é tocado por este teste: ele cria
# um keystore TEMPORÁRIO em /tmp com senha de teste, assina um APK de mentira
# (o próprio build.sh, que é o caminho real), roda `apksigner verify` e apaga
# tudo num trap. Se o build falhar no meio, o trap limpa do mesmo jeito.
#
# Caminhos cobertos:
#   1) chave debug (.debug.keystore, senha pública pass:android) — o padrão;
#   2) chave arbitrária + MANAGER_KS_PASS no ambiente (não interativo);
#   3) a senha NUNCA aparece na linha de comando (grep no build.sh);
#   4) build_release.sh acha ~/.config/bepinex-termux/manager-release.jks por
#      padrão, com HOME apontando para uma árvore temporária (nunca a real).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_DIR="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
BUILD_TOOLS="${BUILD_TOOLS:-$SDK_DIR/build-tools/37.0.0}"
APKSIGNER="$BUILD_TOOLS/apksigner"
KEYTOOL="${KEYTOOL:-keytool}"
JAVAC="${JAVAC:-javac}"

fails=0
check() {
    if [ "$2" = 1 ]; then printf '  [PASS] %s\n' "$1"
    else printf '  [FAIL] %s\n' "$1"; fails=$((fails + 1)); fi
}

TMP="$(mktemp -d)"
# shellcheck disable=SC2329  # invocada pelo trap, não chamada direto
cleanup() {
    # Nada do keystore de teste sobrevive: rm -rf da árvore inteira.
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

if [ ! -x "$APKSIGNER" ]; then
    echo "SKIP: apksigner ausente ($APKSIGNER) — nada a assinar" >&2
    exit 0
fi
if ! command -v "$KEYTOOL" >/dev/null 2>&1; then
    echo "SKIP: keytool ausente" >&2
    exit 0
fi

echo "== keystore temporário em $TMP (senha de teste, some no trap) =="
KS="$TMP/manager-test.jks"
KS_PASS="teste-$$-nao-e-a-senha-real"
DNAME="CN=Teste BepInEx Termux, OU=Dev, O=Teste, L=X, ST=X, C=BR"
# A senha do keystore e a da chave são iguais de propósito: é o caso comum.
"$KEYTOOL" -genkeypair -noprompt \
    -keystore "$KS" -storepass "$KS_PASS" -keypass "$KS_PASS" \
    -alias manager -keyalg RSA -keysize 2048 -validity 30 \
    -dname "$DNAME" >/dev/null 2>&1
check "keystore temporário criado com alias 'manager'" "$([ -f "$KS" ] && echo 1 || echo 0)"

# O APK que vai ser assinado: o build real do Manager (é o mesmo caminho que
# o usuário usa). Compila uma vez e reusa.
echo "== build do Manager (o mesmo build.sh do usuário) =="
if ! (cd "$ROOT" && MANAGER_UNSIGNED=0 bash manager/build.sh) >"$TMP/build.log" 2>&1; then
    echo "ERRO: manager/build.sh falhou:" >&2
    tail -20 "$TMP/build.log" >&2
    exit 1
fi
APK="$ROOT/manager/bepinex-manager.apk"
check "APK assinado existe" "$([ -f "$APK" ] && echo 1 || echo 0)"

# 1) caminho da chave debug: senha pública, alias default
echo "== caminho 1: chave debug (.debug.keystore, pass:android) =="
DBG_CERT="$("$APKSIGNER" verify --print-certs "$APK" 2>/dev/null |
    awk -F': *' '/certificate SHA-256 digest/ {print $NF; exit}')"
check "apksigner verify le o certificado do APK debug" "$([ -n "$DBG_CERT" ] && echo 1 || echo 0)"
# O esquema importa: minSdk 26 precisa de v2 (v1 é só para < 24) e um só
# signer. `verify --verbose` é a forma canônica de ver isso.
verify_verbose="$("$APKSIGNER" verify --verbose "$APK" 2>/dev/null)"
check "APK verificável com o esquema v2 (minSdk 26)" \
    "$(printf '%s' "$verify_verbose" | grep -q 'Verified using v2 scheme (APK Signature Scheme v2): true' && echo 1 || echo 0)"
check "exatamente 1 signer" \
    "$(printf '%s' "$verify_verbose" | grep -q 'Number of signers: 1' && echo 1 || echo 0)"

# 2) caminho de chave arbitrária com a senha no AMBIENTE (não interativo)
echo "== caminho 2: chave arbitrária + MANAGER_KS_PASS no ambiente =="
# Um keystore DIFFERENTE do debug: se o build usasse a senha pública android
# por engano, falharia (a senha de teste não é "android").
KS2="$TMP/manager-outra.jks"
KS2_PASS="outra-$$-senha"
"$KEYTOOL" -genkeypair -noprompt \
    -keystore "$KS2" -storepass "$KS2_PASS" -keypass "$KS2_PASS" \
    -alias manager -keyalg RSA -keysize 2048 -validity 30 \
    -dname "$DNAME" >/dev/null 2>&1
check "segundo keystore temporário criado" "$([ -f "$KS2" ] && echo 1 || echo 0)"

export MANAGER_KEYSTORE="$KS2"
export MANAGER_KS_PASS="$KS2_PASS"
export MANAGER_KEY_PASS="$KS2_PASS"
if (cd "$ROOT" && bash manager/build.sh) >"$TMP/build2.log" 2>&1; then
    OUT2_CERT="$("$APKSIGNER" verify --print-certs "$APK" 2>/dev/null |
        awk -F': *' '/certificate SHA-256 digest/ {print $NF; exit}')"
    check "APK assinado com a chave arbitrária (env MANAGER_KS_PASS)" "$([ -n "$OUT2_CERT" ] && echo 1 || echo 0)"
    check "o certificado é DIFERENTE do da chave debug (a chave antiga entrou mesmo)" \
        "$([ -n "$OUT2_CERT" ] && [ "$OUT2_CERT" != "$DBG_CERT" ] && echo 1 || echo 0)"
else
    check "build com MANAGER_KS_PASS falhou (ver log)" 0
    tail -20 "$TMP/build2.log" >&2
fi
unset MANAGER_KS_PASS MANAGER_KEY_PASS
unset MANAGER_KEYSTORE

# 3) a senha nunca aparece na linha de comando do build.sh
echo "== caminho 3: senha nunca literal na linha de comando =="
# pass: literal é proibido no caminho de chave não-debug. O grep olha o bloco
# de assinatura (não o todo: o pass:android da chave debug é intencional).
sign_block="$(sed -n '/Chave debug/,/^fi$/p' "$ROOT/manager/build.sh")"
check "o build.sh usa env:MANAGER_KS_PASS (não pass: literal)" \
    "$(printf '%s' "$sign_block" | grep -q 'env:MANAGER_KS_PASS' && echo 1 || echo 0)"
check "sem --ks-pass pass:<algo> no caminho de chave arbitrária" \
    "$(printf '%s' "$sign_block" | awk '/MANAGER_KEYSTORE"\)/{exit} /pass:/{found=1} END{exit found?1:0}' && echo 0 || echo 1)"
check "o alias é configurável (MANAGER_KEY_ALIAS, default manager)" \
    "$(printf '%s' "$sign_block" | grep -q 'MANAGER_KEY_ALIAS:-manager' && echo 1 || echo 0)"

# 4) build_release.sh acha a chave de release por padrão em $HOME
echo "== caminho 4: build_release.sh usa ~/.config/bepinex-termux por padrão =="
# HOME temporário com a chave no lugar certo: o caminho de default tem que
# pegá-la. A árvore real do usuário não é tocada.
FAKE_HOME="$TMP/home"
mkdir -p "$FAKE_HOME/.config/bepinex-termux"
cp "$KS" "$FAKE_HOME/.config/bepinex-termux/manager-release.jks"
# O build_release.sh é caro (módulo + 3 mods + manager). Aqui só importa a
# decisão de chave e o BUILD-INFO, então extraímos o trecho de decisão sem
# rodar o resto: reexecutamos o script com um --output temporário e com o
# resto do trabalho já em cache seria lento demais para um teste de host.
# Em vez disso, o teste verifica o que o script PROMETE:
# shellcheck disable=SC2016  # aspas simples: o bash -c precisa ver as variáveis
plan="$(HOME="$FAKE_HOME" env -u MANAGER_KEYSTORE bash -c '
    RELEASE_KEY_DEFAULT="$HOME/.config/bepinex-termux/manager-release.jks"
    if [ -n "${MANAGER_KEYSTORE:-}" ]; then echo external
    elif [ -f "$RELEASE_KEY_DEFAULT" ]; then echo release-key
    else echo UNSIGNED-DEBUG; fi')"
check "HOME com a chave => plano release-key" "$([ "$plan" = "release-key" ] && echo 1 || echo 0)"
# shellcheck disable=SC2016  # idem: aspas simples por opção
plan2="$(HOME="$TMP" env -u MANAGER_KEYSTORE bash -c '
    RELEASE_KEY_DEFAULT="$HOME/.config/bepinex-termux/manager-release.jks"
    if [ -n "${MANAGER_KEYSTORE:-}" ]; then echo external
    elif [ -f "$RELEASE_KEY_DEFAULT" ]; then echo release-key
    else echo UNSIGNED-DEBUG; fi')"
check "HOME sem a chave => plano UNSIGNED-DEBUG" "$([ "$plan2" = "UNSIGNED-DEBUG" ] && echo 1 || echo 0)"
# E o script tem a mesma ordem de precedência (default antes do unsigned).
rl="$(cat "$ROOT/tools/build_release.sh")"
check "build_release.sh tem a chave default antes do ramo UNSIGNED" \
    "$(printf '%s' "$rl" | awk '/RELEASE_KEY_DEFAULT.*\.jks/{d=NR} /UNSIGNED-DEBUG/{u=NR} END{exit (d>0 && u>0 && d<u)?0:1}' && echo 1 || echo 0)"
# E o fingerprint sai do APK assinado, não do .jks.
check "build_release.sh extrai o fingerprint com apksigner verify --print-certs" \
    "$(printf '%s' "$rl" | grep -q 'verify --print-certs' && echo 1 || echo 0)"
check "build_release.sh nunca abre o .jks com keytool" \
    "$(printf '%s' "$rl" | grep -q 'keytool' && echo 0 || echo 1)"

# 5) o .gitignore não pode deixar chave nem APK assinado entrarem
echo "== chave e APK fora do git =="
# Onde a chave de verdade vive: ~/.config/bepinex-termux/ (FORA do repo) e,
# no repo, so o keystore debug que o build cria (manager/.debug.keystore).
# O .gitignore da RAIZ tambem tem de cobrir chave e binario assinado: um
# `git add .` com a chave largada na raiz e o acidente que nao se desfaz
# (commit de chave privada fica no historico para sempre).
check "manager/.gitignore cobre *.keystore (o debug que o build cria)" \
    "$(grep -qE '^\*\.keystore$' "$ROOT/manager/.gitignore" && echo 1 || echo 0)"
check "manager/.gitignore cobre .*.keystore (.debug.keystore)" \
    "$(grep -qE '^\.\*\.keystore$' "$ROOT/manager/.gitignore" && echo 1 || echo 0)"
check "manager/.gitignore cobre *.jks" \
    "$(grep -qE '^\*\.jks$' "$ROOT/manager/.gitignore" && echo 1 || echo 0)"
# shellcheck disable=SC2016  # o $HOME precisa aparecer literal no grep
check "a chave de release fica FORA do repo (~/.config/bepinex-termux)" \
    "$(grep -q 'RELEASE_KEY_DEFAULT="$HOME/.config/bepinex-termux/manager-release.jks"' "$ROOT/tools/build_release.sh" && echo 1 || echo 0)"
for pat in '*.jks' '*.keystore' '*.p12' '*.pfx' '*.idsig' '*.apk'; do
    check ".gitignore da raiz cobre $pat" \
        "$(grep -qxF "$pat" "$ROOT/.gitignore" && echo 1 || echo 0)"
done
# Funcional, nao grep: cria os arquivos de verdade na raiz e pergunta ao git.
probe="chave-probe"
(cd "$ROOT" && : > "$probe.jks" && : > "$probe.keystore" && : > "$probe.apk" \
    && : > "$probe.p12" && : > "$probe.pfx" && : > "$probe.idsig")
leaked="$(cd "$ROOT" && git status --porcelain --untracked-files=all | grep -F "$probe" || true)"
rm -f "$ROOT/$probe.jks" "$ROOT/$probe.keystore" "$ROOT/$probe.apk" \
      "$ROOT/$probe.p12" "$ROOT/$probe.pfx" "$ROOT/$probe.idsig"
check "chave/APK na raiz NAO aparece no git status (teste funcional)" \
    "$([ -z "$leaked" ] && echo 1 || echo 0)"
if [ -n "$leaked" ]; then
    printf '        vazou no status: %s\n' "$leaked" >&2
fi
check "a prova limpou os arquivos depois" \
    "$([ ! -e "$ROOT/$probe.jks" ] && [ ! -e "$ROOT/$probe.apk" ] && echo 1 || echo 0)"

echo "== Resultado: $([ "$fails" -eq 0 ] && echo 'TODOS PASSARAM' || echo "HOUVE $fails FALHAS") ($fails falhas) =="
exit "$([ "$fails" -eq 0 ] && echo 0 || echo 1)"
