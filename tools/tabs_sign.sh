#!/usr/bin/env bash
# tools/tabs_sign.sh — alinha e assina o APK do TABS com keystore PRÓPRIA.
#
# POR QUE UM SCRIPT E NÃO COMANDO SOLTO. Reassinar APK do TABS é uma cadeia de
# três passos em que errar um só dá APK que instala e crasha na hora: zipalign
# (o APK original é assinado e alinhado pelo Google; a surgery de
# tools/tabs_patch.py muda offsets), assinatura v2/v3 com keystore nova (a
# assinatura do Google não pode ser reutilizada), e verificação. Repetir essa
# cadeia à mão é como a evidência se perde: o apk final que entra no aparelho
# tem que ter o sha256 que o relatório diz.
#
# A KEYSTORE É NOSSA E SÓ DO TABS. ~/.config/tabs-mod/tabs.jks, gerada na
# primeira vez com keytool. NÃO usa, NÃO lê e NÃO copia a keystore do Manager
# (~/.config/bepinex-termux/manager-release.jks): assinar o APK do jogo com a
# chave que assina o módulo do loader misturaria as duas identidades, e um
# vazamento de uma expõe a outra. Nada disso vai para o git.
#
# O que a assinatura nova QUEBRA (esperado, e é o preço de modificar o APK):
#   - atualizações pela Play Store param (a assinatura não bate com a do Play);
#   - reinstalar exige desinstalar antes (o Android recusa assinatura diferente),
#     e desinstalar apaga o data dir — por isso o backup antes de trocar;
#   - TapTap.License / Google Play Games podem recusar a assinatura nova.
#     Isso é medido no device, não suposto: ver o relatório.
#
# Uso: tools/tabs_sign.sh <apk-patchado> <apk-assinado> [keystore-dir]
set -euo pipefail

die() { echo "ERRO: $*" >&2; exit 1; }
ok() { echo "tabs_sign: $*"; }

IN=${1:?uso: tools/tabs_sign.sh <apk-patchado> <apk-assinado> [keystore-dir]}
OUT=${2:?uso: tools/tabs_sign.sh <apk-patchado> <apk-assinado> [keystore-dir]}
KSDIR=${3:-$HOME/.config/tabs-mod}

[ -f "$IN" ] || die "APK de entrada ausente: $IN"
command -v keytool >/dev/null 2>&1 || die "keytool não encontrado (JDK)"
BT=${ANDROID_BUILD_TOOLS:-}
if [ -z "$BT" ]; then
    for cand in "$HOME"/Android/Sdk/build-tools/*; do
        [ -x "$cand/apksigner" ] && BT=$cand
    done
fi
[ -n "$BT" ] || die "apksigner/zipalign não encontrado: defina ANDROID_BUILD_TOOLS"
[ -x "$BT/apksigner" ] || die "$BT/apksigner não é executável"
[ -x "$BT/zipalign" ] || die "$BT/zipalign não é executável"

# --- keystore própria (gerada uma vez, nunca versionada) --------------------
mkdir -p "$KSDIR"
chmod 700 "$KSDIR"
KS="$KSDIR/tabs.jks"
PASS_FILE="$KSDIR/keystore.pass"
if [ ! -f "$KS" ]; then
    [ -f "$PASS_FILE" ] || die "keystore ausente e $PASS_FILE também: foi criado só o pass?"
    PASS=$(cat "$PASS_FILE")
    keytool -genkeypair -v -keystore "$KS" -storetype PKCS12 \
        -storepass "$PASS" -keypass "$PASS" -alias tabsmod \
        -keyalg RSA -keysize 4096 -validity 10950 \
        -dname "CN=tabs-mod (APK local do TABS), OU=mods locais, O=rianprei, L=-, ST=-, C=BR" \
        >/dev/null 2>&1 || die "keytool falhou ao gerar $KS"
    chmod 600 "$KS" "$PASS_FILE"
    ok "keystore criada em $KS (RSA 4096, 30 anos, alias tabsmod)"
else
    [ -f "$PASS_FILE" ] || die "keystore existe mas $PASS_FILE não: sem a senha não dá para assinar"
    ok "keystore reaproveitada: $KS"
fi
case "$KS" in
    "$HOME"/*) : ;;
    *) die "a keystore tem que ficar dentro de \$HOME: $KS" ;;
esac
# Confere que a keystore é a NOSSA e não a do Manager: mesmo alias, mesmo
# subject. Sem essa checagem, "apontar KSDIR para a pasta do Manager" passaria
#batido e o APK do jogo sairia assinado com a chave do loader — duas
# identidades diferentes no mesmo lugar, e um vazamento de uma expõe a outra.
# O subject sai do openssl quando ele existe; senão do keytool com LC_ALL=C,
# porque em pt_BR o rótulo é "Proprietário:" e o sed por "Owner:" não acha
# nada (e um check que não acha nada passa).
SUBJ=""
if command -v openssl >/dev/null 2>&1; then
    SUBJ=$(openssl pkcs12 -in "$KS" -nokeys -passin "file:$PASS_FILE" 2>/dev/null \
           | openssl x509 -noout -subject 2>/dev/null | sed -n 's/^subject=//p')
fi
if [ -z "$SUBJ" ]; then
    SUBJ=$(LC_ALL=C keytool -list -v -keystore "$KS" -storepass "$(cat "$PASS_FILE")" -alias tabsmod 2>/dev/null \
           | sed -n 's/^Owner: //p' | head -n1)
fi
[ -n "$SUBJ" ] || die "não consegui ler o subject de $KS: a keystore está corrompida?"
case "$SUBJ" in
    *CN=tabs-mod*) ok "subject confere: $SUBJ" ;;
    *) die "a keystore $KS não é a do tabs-mod (subject='$SUBJ'). Se você quer usar a do Manager, pare e reavalie: são duas identidades diferentes." ;;
esac

# --- zipalign --------------------------------------------------------------
ALIGNED="${OUT%.apk}.aligned.apk"
rm -f "$ALIGNED"
# -p: alinha as .so em página de 4 KB (o APK do Unity tem extractNativeLibs
# decide; alinhar nunca atrapalha). -f: reescreve do zero, que é o que a
# surgery de offsets exige.
"$BT/zipalign" -p -f 4 "$IN" "$ALIGNED" || die "zipalign falhou"
ok "zipalign: $ALIGNED"

# --- assinatura ------------------------------------------------------------
rm -f "$OUT"
# A senha vai por variável de ambiente, não por argv: em file: o apksigner lê
# a linha e reclama de EOF quando o arquivo termina com \n; em pass: a senha
# fica visível em `ps` para qualquer outro usuário da máquina. env: resolve os
# dois.
KS_PASS=$(cat "$PASS_FILE")
export KS_PASS
"$BT/apksigner" sign \
    --ks "$KS" --ks-pass env:KS_PASS --key-pass env:KS_PASS \
    --ks-key-alias tabsmod \
    --min-sdk-version 25000 \
    --out "$OUT" "$ALIGNED" || die "apksigner sign falhou"
ok "assinado: $OUT"

# --- verificação (sem isto, "instalado" não quer dizer nada) -----------------
"$BT/zipalign" -c -v 4 "$OUT" >/dev/null || die "zipalign -c reprovou o APK assinado"
"$BT/apksigner" verify --min-sdk-version 25000 --print-certs "$OUT" \
    | sed -n '1,6p' | sed 's/^/tabs_sign: /'
"$BT/apksigner" verify --min-sdk-version 25000 "$OUT" || die "apksigner verify reprovou"
rm -f "$ALIGNED"
ok "verificado: alinhado e assinatura válida (v1+v2+v3)"
ok "sha256: $(sha256sum "$OUT" | cut -d' ' -f1)"
