#!/bin/sh
# T1 kit de validação no device. QUEM EXECUTA É O USUÁRIO (agente não usa adb):
#   tools/device_test.sh <pkg> <dir-de-teste> [timeout_s] [--no-frida] [--dry-run] [--force]
#       [--hold-after-pass=seconds]
#   (flags sempre DEPOIS dos dois posicionais)
#
# Faz: recuperação de run interrompido (com o estado atual de mods na mão
# ANTES de tocar; diferença do backup = conflito, nada é apagado) →
# snapshot inicial (lista+sha256 de mods/<pkg>/ E de files/bepinex INTEIRA,
# incluindo subpastas vazias) → hash inicial por árvore (ausência só pela
# marca AUSENTE do device; falha de leitura aborta sem tocar nada) →
# backup dos dois (só depois de conferir o hash da cópia) → marca run em
# andamento no device → instala artefatos via staging+su → limpa logs →
# logcat -c → abre o jogo → espera as linhas de expect.txt no log.txt →
# checa crash (pid morto / Fatal signal do PID) → PASS/FAIL → RESTAURA TUDO
# e CONFERE (snapshot depois tem de ser idêntico ao inicial).
# Saída 0 = tudo PASS sem crash; 1 = qualquer FAIL/crash/erro de teste;
#        2 = RESTAURAÇÃO FALHOU (device diferente do inicial);
#      130 = interrompido (Ctrl-C), depois de restaurar.
#
# --no-frida: pula *.js (SKIP com aviso, não FAIL). --dry-run: imprime os
# comandos adb sem executar nada (pra revisar o script sem device).
# Provas locais, sem device: test/device/quoting-check.sh (entrega via stdin
# pro su) e test/device/restore-sim.sh (run interrompido, snapshot vazio,
# interrupção com restauração — usa o gancho T1_SIM_EXIT deste script).
#
# AVISO: ao terminar o jogo fica PARADO (force-stop no restore) — é
# intencional, mas significa que o device não volta ao estado de runtime
# inicial. O que volta é o estado de ARQUIVOS (mods e files/bepinex).
set -eu

PKG=${1:?uso: tools/device_test.sh <pkg> <dir-de-teste> [timeout_s] [--no-frida] [--dry-run] [--hold-after-pass=seconds]}
TDIR=${2:?uso: tools/device_test.sh <pkg> <dir-de-teste> [timeout_s] [--no-frida] [--dry-run] [--hold-after-pass=seconds]}
shift 2
TIMEOUT=120
NO_FRIDA=0
DRY=0
FORCE=0
HOLD_AFTER_PASS=0
for a in "$@"; do
    case "$a" in
        --no-frida) NO_FRIDA=1;;
        --dry-run) DRY=1;;
        --force) FORCE=1;;
        --hold-after-pass=*)
            HOLD_AFTER_PASS=${a#*=}
            case "$HOLD_AFTER_PASS" in
                ''|*[!0-9]*) echo "FAIL: duração inválida: $a"; exit 1;;
            esac
            HOLD_AFTER_PASS=$(printf '%s' "$HOLD_AFTER_PASS" | sed 's/^0*//')
            [ -n "$HOLD_AFTER_PASS" ] || HOLD_AFTER_PASS=0
            if [ "$HOLD_AFTER_PASS" -lt 1 ] || [ "$HOLD_AFTER_PASS" -gt 600 ]; then
                echo "FAIL: --hold-after-pass deve estar entre 1 e 600 segundos"; exit 1
            fi
            ;;
        ''|*[!0-9]*) echo "FAIL: arg inválido: $a"; exit 1;;
        *) TIMEOUT=$a;;
    esac
done

MODS="/data/local/tmp/mods/$PKG"
STAGE="/data/local/tmp/t1-stage-$PKG"
BAK="/data/local/tmp/t1-bak-$PKG"
BAK_OUT="/data/local/tmp/t1-bak-out-$PKG"
MARK="/data/local/tmp/t1-inprogress-$PKG"
LOCK="/data/local/tmp/t1-lock-$PKG"
LOCK_MAX_AGE_S=900   # 15 min: run vivo aborta o próximo; depois disso, órfão
# ADB por caminho absoluto: o restore-sim passa o falso dele aqui, então nem
# uma quebra de PATH pode fazer o kit alcançar um adb real (o sim blinda com
# ANDROID_SERIAL/ADB_SERVER_SOCKET e guarda de resolução — ver restore-sim).
ADB=${ADB:-adb}
OUT="/data/data/$PKG/files/bepinex"
EXPECT="$TDIR/expect.txt"
HOST_TMP="$(mktemp -d)"
BEFORE_FILE="$HOST_TMP/before.txt"
RESTORE_DONE=0
# set -u: restore (trap) pode rodar antes de qualquer backup — as HASH_* têm
# que existir desde o começo, e o lock só é solto por quem o pegou.
LOCK_HELD=0
HASH_MODS_INITIAL=
HASH_OUT_INITIAL=
# PKG como regex: ponto literal (com.foo não casa com comXfoo nem com com.foobar)
# shellcheck disable=SC2016  # o $ dentro das aspas simples é literal de propósito
PKG_RE=$(printf '%s' "$PKG" | sed 's/[.[\*^$()+?{|]/\\&/g')

# Todos os toques no device passam daqui (dry-run imprime no stderr pra não
# poluir capturas $(...)).
# ENTREGA VIA STDIN DE PROPÓSITO: `adb shell su -c "$cmd"` junta os args com
# espaço e o sh do device re-divide — `su -c test -d X && rm ...` vira su
# rodando só `test` e o resto como shell sem root (achado no device:
# chmod/rm/mv com Permission denied). Via stdin o comando chega intacto,
# com && e aspas (prova em test/device/quoting-check.sh).
dev() {
    if [ "$DRY" = 1 ]; then echo "DRY> printf '%s' | $ADB shell su : $1" >&2; else printf '%s\n' "$1" | "$ADB" shell su; fi
}
do_adb() {
    if [ "$DRY" = 1 ]; then echo "DRY> $ADB $*" >&2; else "$ADB" "$@"; fi
}

# Estado canônico: lista + modo + sha256 de TUDO dentro de mods/<pkg>/ e de
# files/bepinex/ (inclusive arquivos que o teste cria: crashguard, dump.tsv,
# log.txt.t1bak...). Subpastas VAZIAS também contam (find -type d -empty):
# sem elas o diff final é cego pra diretório que o teste criou/apagou.
# Sem Pastas próprias, sem wildcard solto.
snapshot_to() {
    _snapshot_tmp="$HOST_TMP/snapshot.part"
    rm -f "$_snapshot_tmp"
    if ! dev "_t1_tmp=/data/local/tmp/t1-snapshot-\$\$
        mkdir \"\$_t1_tmp\" || exit 1
        _t1_fail() { rm -rf \"\$_t1_tmp\"; exit 1; }
        for d in $MODS $OUT; do
            if [ -d \"\$d\" ]; then
                stat -c '%n %a %U:%G' \"\$d\" || _t1_fail
                find \"\$d\" -type d -empty > \"\$_t1_tmp/dirs\" || _t1_fail
                sort -o \"\$_t1_tmp/dirs\" \"\$_t1_tmp/dirs\" || _t1_fail
                while IFS= read -r _dir; do
                    stat -c '%n %a %U:%G' \"\$_dir\" || _t1_fail
                done < \"\$_t1_tmp/dirs\"
                find \"\$d\" -type f > \"\$_t1_tmp/files\" || _t1_fail
                sort -o \"\$_t1_tmp/files\" \"\$_t1_tmp/files\" || _t1_fail
                while IFS= read -r _file; do
                    stat -c '%n %s %a' \"\$_file\" || _t1_fail
                    _sum=\$(sha256sum \"\$_file\") || _t1_fail
                    printf '%s\\n' \"\$_sum\" | grep -Eq '^[0-9a-f]{64}  ' || _t1_fail
                    printf '%s\\n' \"\$_sum\" || _t1_fail
                done < \"\$_t1_tmp/files\"
            else
                echo \"\$d AUSENTE\"
            fi
        done
        rm -rf \"\$_t1_tmp\" || exit 1" > "$_snapshot_tmp" 2>/dev/null; then
        rm -f "$_snapshot_tmp"
        return 1
    fi
    mv "$_snapshot_tmp" "$1"
}

# Hash do CONTEÚDO de uma pasta, independente do caminho (nomes relativos),
# para provar que um backup é mesmo cópia do que estava lá. Subpastas vazias
# entram no hash (mesma cegueira do snapshot não pode existir aqui).
# Ausência é MARCA EXPLÍCITA do device (test -d → AUSENTE), nunca silêncio:
# saída vazia = adb/su falhou, e quem chama tem que recusar (is_hash), não
# adivinhar "não existia" — inferir ausência de silêncio já apagou dado real.
tree_hash() {
    _tree_result=$(dev "if [ ! -d '$1' ]; then echo AUSENTE; exit 0; fi
        _t1_tmp=/data/local/tmp/t1-tree-hash-\$\$
        mkdir \"\$_t1_tmp\" || { echo ERROR; exit 0; }
        _t1_fail() { rm -rf \"\$_t1_tmp\"; echo ERROR; exit 0; }
        if ! (
            cd '$1' || exit 1
            stat -c '%n %a %U:%G' . > \"\$_t1_tmp/records\" || exit 1
            find . -type d -empty > \"\$_t1_tmp/dirs\" || exit 1
            sort -o \"\$_t1_tmp/dirs\" \"\$_t1_tmp/dirs\" || exit 1
            while IFS= read -r _dir; do
                stat -c 'EMPTY %n %a' \"\$_dir\" >> \"\$_t1_tmp/records\" || exit 1
            done < \"\$_t1_tmp/dirs\"
            find . -type f > \"\$_t1_tmp/files\" || exit 1
            sort -o \"\$_t1_tmp/files\" \"\$_t1_tmp/files\" || exit 1
            while IFS= read -r _file; do
                stat -c '%n %s %a' \"\$_file\" >> \"\$_t1_tmp/records\" || exit 1
                _sum=\$(sha256sum \"\$_file\") || exit 1
                printf '%s\\n' \"\$_sum\" | grep -Eq '^[0-9a-f]{64}  ' || exit 1
                printf '%s\\n' \"\$_sum\" >> \"\$_t1_tmp/records\" || exit 1
            done < \"\$_t1_tmp/files\"
        ); then _t1_fail; fi
        _sum=\$(sha256sum \"\$_t1_tmp/records\") || _t1_fail
        printf '%s\\n' \"\$_sum\" | grep -Eq '^[0-9a-f]{64}  ' || _t1_fail
        rm -rf \"\$_t1_tmp\" || { echo ERROR; exit 0; }
        printf '%s\\n' \"\$_sum\" | cut -d' ' -f1" 2>/dev/null) || _tree_result=ERROR
    _tree_result=$(printf '%s' "$_tree_result" | tr -d '\r')
    case "$_tree_result" in
        *"
"*) printf 'ERROR'; return 0;;
    esac
    if printf '%s\n' "$_tree_result" | grep -Eq '^[0-9a-f]{64}$|^AUSENTE$'; then
        printf '%s' "$_tree_result"
    else
        printf 'ERROR'
    fi
}

# Hash de verdade (64 hex): AUSENTE e vazio NÃO passam.
is_hash() { printf '%s' "$1" | grep -qE '^[0-9a-f]{64}$'; }

# Restaura UMA árvore, e só apaga depois de provar que o backup é o original.
# Sem backup verificado: NÃO APAGA NADA e devolve 1 (o restore vira exit 2, e o
# marcador fica de pé para o próximo run recuperar). Nada de rm de dado do
# usuário sem backup correspondente conferido — era o que apagava o
# files/bepinex inteiro (dump.tsv do u_dump) quando o BAK_OUT não existia.
restore_one() {
    _what=$1 _live=$2 _bak=$3 _hash=$4
    if ! is_hash "$_hash"; then
        # AUSENTE só vale com o snapshot inicial confirmando a ausência (a
        # marca "AUSENTE" no before.txt). Silêncio/contradição é ambíguo:
        # recusa — inferir ausência de saída vazia já apagou pasta existente.
        if [ "$_hash" = AUSENTE ] && grep -qF "${_live} AUSENTE" "$BEFORE_FILE" 2>/dev/null; then
            dev "rm -rf $_live" || return 1
            return 0
        fi
        echo "  $_what: estado inicial ausente/ilegível ($_hash) e não confirmado pelo snapshot — NADA APAGADO"
        return 1
    fi
    _bakh=$(tree_hash "$_bak")
    if ! is_hash "$_bakh"; then
        echo "  $_what: sem backup em $_bak — NADA APAGADO (rode de novo com backup na mão)"
        return 1
    fi
    if [ "$_bakh" != "$_hash" ]; then
        echo "  $_what: backup em $_bak não bate com o estado inicial ($_bakh != $_hash) — NADA APAGADO"
        return 1
    fi
    dev "rm -rf $_live && cp -a $_bak $_live" || return 1
    return 0
}

restore_tree() {
    RC_TREE=0
    if ! do_adb shell am force-stop "$PKG" >/dev/null 2>&1; then
        echo "  force-stop falhou; a restauração não pode ser confirmada enquanto o jogo puder gravar arquivos"
        RC_TREE=1
    fi
    restore_one "mods/$PKG" "$MODS" "$BAK" "$HASH_MODS_INITIAL" || RC_TREE=1
    restore_one "files/bepinex" "$OUT" "$BAK_OUT" "$HASH_OUT_INITIAL" || RC_TREE=1
    # staging e .part vão sempre (são nossos); o marcador e o BAK_OUT só com
    # as duas árvores de volta no lugar — senão o próximo run não tem como
    # recuperar. O BAK (mods) NÃO sai aqui: sai no restore(), depois do diff
    # final e de conferir a árvore restaurada contra ele (hash) — nunca
    # apagar backup sem prova.
    if [ "$RC_TREE" = 0 ]; then
        dev "rm -rf $STAGE $BAK.part $BAK_OUT.part $BAK_OUT" || true
        dev "rm -f $MARK" || true
    else
        echo "  aviso: marcador e backup mantidos para o próximo run recuperar"
    fi
    lock_drop
    return $RC_TREE
}

restore() {
    [ "$RESTORE_DONE" = 1 ] && return 0
    RESTORE_DONE=1
    trap - EXIT INT TERM HUP
    if [ "$DRY" = 1 ]; then
        echo "--- (dry-run) restauraria device ---"
        return 0
    fi
    echo "--- restaurando device ---"
    if restore_tree; then
        TREE_OK=1
    else
        TREE_OK=0
        echo "RESTAURACAO INCOMPLETA: alguma árvore nao tem backup verificado"
    fi
    if ! snapshot_to "$HOST_TMP/after.txt"; then
        echo "RESTAURACAO FALHOU (snapshot pós-restauração incompleto: arquivo sem SHA-256)"
        rm -rf "$HOST_TMP"
        exit 2
    fi
    if [ ! -s "$BEFORE_FILE" ]; then
        echo "RESTAURACAO FALHOU (snapshot inicial vazio — nada conferível)"
        rm -rf "$HOST_TMP"
        exit 2
    fi
    if [ ! -s "$HOST_TMP/after.txt" ]; then
        echo "RESTAURACAO FALHOU (snapshot pós-restauração vazio — adb sem resposta?)"
        rm -rf "$HOST_TMP"
        exit 2
    fi
    if [ "$TREE_OK" = 1 ] && diff -q "$BEFORE_FILE" "$HOST_TMP/after.txt" >/dev/null 2>&1; then
        echo "device restaurado (mods + files/bepinex idênticos ao inicial)"
        # (b) O backup de mods também some no sucesso VERIFICADO — antes ele
        # ficava vivo e todo run seguinte caía em "run anterior interrompido".
        # Mesma regra de sempre: só apaga com prova. Aqui a prova é dupla — o
        # snapshot final já é idêntico ao inicial (diff acima) e a árvore
        # restaurada ainda tem que conferir contra o próprio BAK (hash):
        # se o hash não bater, o BAK fica (fail-safe, aviso no log).
        if dev "test -d $BAK"; then
            _restored=$(tree_hash "$MODS")
            _bakh=$(tree_hash "$BAK")
            if is_hash "$_restored" && is_hash "$_bakh" && [ "$_restored" = "$_bakh" ]; then
                dev "rm -rf $BAK" || true
            else
                echo "  aviso: BAK de mods mantido (árvore restaurada não confere contra ele)"
            fi
        fi
        rm -rf "$HOST_TMP"
        return 0
    fi
    echo "RESTAURACAO FALHOU (estado difere do inicial; ver diff)"
    echo "Registros divergentes (caminhos, metadados e SHA-256):"
    diff -u "$BEFORE_FILE" "$HOST_TMP/after.txt" 2>/dev/null || true
    rm -rf "$HOST_TMP"
    exit 2
}

# --- precondições (sem trap ainda: falhar aqui não é "restauração falhou") ---
[ -d "$TDIR" ] || { echo "FAIL: dir de teste ausente: $TDIR"; exit 1; }
[ -f "$EXPECT" ] || { echo "FAIL: sem expect.txt em $TDIR"; exit 1; }
if [ "$DRY" = 0 ]; then
    command -v "$ADB" >/dev/null 2>&1 || { echo "FAIL: adb não encontrado ($ADB)"; exit 1; }
    [ "$("$ADB" get-state 2>/dev/null)" = "device" ] || { echo "FAIL: device não conectado"; exit 1; }
    # </dev/null DE PROPÓSITO: com adb real isso é inócuo (su -c não lê stdin),
    # e no restore-sim o su FALSO é um filtro de stream (sed|sh) que espera EOF
    # no stdin herdado — sem o redirect o kit bloqueia no stdin do processo
    # que o chamou (PTY do gate; achado 2026-09-27: trava de 300s no gate,
    # sed/sh presos em anon_pipe_read, evidência ps/wchan no fim do arquivo).
    "$ADB" shell su -c true </dev/null 2>/dev/null || { echo "FAIL: su sem resposta (root?)"; exit 1; }
    dev "command -v sha256sum" >/dev/null || { echo "FAIL: sem sha256sum no device (restauração não verificável)"; exit 1; }
else
    echo "DRY-RUN: nenhum comando executa de verdade"
fi

# --- recuperação de um run anterior interrompido (nunca rm -rf do BAK) ------
# Se sobrou marcador, ou backup sem a pasta de mods, o estado é de run morto
# (SIGKILL, adb caiu, terminal fechado): o backup é a única cópia limpa dos
# mods do usuário, então ela volta ANTES de qualquer outra coisa. Sem isso o
# run seguinte tiraria o snapshot do estado sujo como "inicial" e a limpeza
# viraria permanente e invisível.
#
# Regra da revisão: o que está em $MODS AGORA é visto e hasheado ANTES de
# qualquer rm — se difere do backup (o usuário pode ter mexido depois do
# crash), NADA é apagado: o backup ganha cópia em $BAK.conflict e o run
# aborta pedindo decisão. Recuperação que não confere (cp falhou/meio
# copiado) também aborta com o $BAK bom intacto — seguir adiante deixaria
# o backup novo (do estado quebrado) destruir o único backup bom.
recover_mods_from_backup() {
    _rm1=$1
    dev "rm -rf $MODS && cp -a $BAK $MODS" || true
    _rnow=$(tree_hash "$MODS")
    if is_hash "$_rnow" && [ "$_rnow" = "$_rm1" ]; then
        # restaurado e conferido: o backup orfao virou redundante
        dev "rm -rf $BAK" || true
        echo "       mods restaurados do backup (hash conferido), backup consumido"
        return 0
    fi
    echo "       AVISO: recuperação não confere — backup mantido em $BAK;"
    echo "       run abortado antes de qualquer backup novo (o $BAK bom não é sobrescrito)."
    return 1
}

if [ "$DRY" = 0 ]; then
    if dev "test -f $MARK || test -d $BAK"; then
        echo "AVISO: run anterior interrompido detectado (marcador ou backup em $BAK)"
        _rmods=$(tree_hash "$BAK")
        _rout=$(tree_hash "$BAK_OUT")
        if is_hash "$_rmods"; then
            # O que existe HOJE em mods/<pkg>, antes de qualquer mudança: o
            # conteúdo atual é listado AQUI (o snapshot de baixo roda depois
            # da recuperação, não serve pra isso).
            _cur_files=$(dev "if [ -d $MODS ]; then find $MODS -type f 2>/dev/null | sort; fi" | tr -d '\r' || true)
            if [ -n "$_cur_files" ]; then
                echo "       conteúdo atual de mods/$PKG (antes de qualquer mudança):"
                printf '%s\n' "$_cur_files" | sed 's/^/         /'
            fi
            _cur=$(tree_hash "$MODS")
            if ! is_hash "$_cur" && [ "$_cur" != AUSENTE ]; then
                # hash vazio = adb/su falhou; se a pasta EXISTE, é ambíguo
                # (não dá pra provar que ela não tem nada a perder) — recusa.
                if dev "test -d $MODS"; then
                    echo "       AVISO: não consegui hashear o mods/$PKG atual (adb/su?) — NADA APAGADO."
                    echo "       Rode de novo com o device respondendo."
                    rm -rf "$HOST_TMP"
                    exit 1
                fi
            fi
            if is_hash "$_cur" && [ "$_cur" != "$_rmods" ]; then
                # diferença real: pode ser mods que o usuário adicionou depois
                # do crash. Nada é apagado; backup preservado em dois lugares.
                dev "rm -rf $BAK.conflict && cp -a $BAK $BAK.conflict" || true
                echo "       AVISO: mods/$PKG atual DIFERE do backup do run morto — NADA APAGADO."
                echo "       backup preservado em $BAK e cópia em $BAK.conflict."
                echo "       Decida na mão (junte os mods que quer manter em mods/$PKG e"
                echo "       apague o marcador $MARK) e rode de novo."
                rm -rf "$HOST_TMP"
                exit 1
            fi
            if recover_mods_from_backup "$_rmods"; then
                :
            else
                rm -rf "$HOST_TMP"
                exit 1
            fi
        elif [ "$_rmods" = AUSENTE ]; then
            echo "       AVISO: sem backup de mods em $BAK — nada apagado"
        else
            # backup presente mas hash ilegível (adb/su falhou): não dá pra
            # conferir, e seguir destruiria o único backup no rm do backup novo.
            echo "       AVISO: backup em $BAK presente mas ilegível — nada apagado, nada sobrescrito."
            echo "       Confira $BAK na mão (e apague o marcador $MARK se decidir descartá-lo)."
            rm -rf "$HOST_TMP"
            exit 1
        fi
        if is_hash "$_rout"; then
            dev "rm -rf $OUT && cp -a $BAK_OUT $OUT" || true
            echo "       files/bepinex restaurado do backup"
        elif [ "$_rout" = AUSENTE ]; then
            if [ ! -d "$OUT" ]; then
                echo "       files/bepinex nao existe (nada a restaurar)"
            else
                echo "       AVISO: sem backup de files/bepinex em $BAK_OUT — pasta do usuario INTACTA"
            fi
        else
            echo "       AVISO: backup de files/bepinex em $BAK_OUT ilegível — pasta do usuario INTACTA"
        fi
        dev "rm -rf $STAGE $BAK.part $BAK_OUT.part" || true
        dev "rm -f $MARK" || true
    fi
fi

echo "--- snapshot inicial (mods + files/bepinex) ---"
if ! snapshot_to "$BEFORE_FILE"; then
    echo "FAIL: snapshot inicial incompleto (arquivo sem SHA-256 ou leitura falhou) — nada foi tocado"
    rm -rf "$HOST_TMP"
    exit 1
fi
if [ "$DRY" = 0 ] && [ ! -s "$BEFORE_FILE" ]; then
    echo "FAIL: snapshot inicial vazio (adb sem resposta? pasta ausente?)"
    rm -rf "$HOST_TMP"
    exit 1
fi
if [ "$DRY" = 0 ]; then
    echo "  $(wc -l < "$BEFORE_FILE" | tr -d ' ') linhas de estado registradas"
fi

# Hash inicial por árvore ANTES do lock e do trap: é leitura pura, e falha
# aqui = abortar sem ter tocado nada (sem lock, sem restore barulhento).
# Ausência só conta com a marca AUSENTE do device; hash VAZIO é falha de
# adb/su — aborta, porque o restore confia nesses hashes pra decidir o que
# pode apagar (silêncio já foi tratado como "não existia" e apagou pasta real).
if [ "$DRY" = 1 ]; then
    echo "--- (dry-run) faria hash inicial de $MODS e $OUT, backup das duas em .part+rename"
    echo "--- (dry-run) com conferencia de hash de cada, e gravaria o marcador $MARK ---"
    HASH_MODS_INITIAL=DRY
    HASH_OUT_INITIAL=DRY
else
    HASH_MODS_INITIAL=$(tree_hash "$MODS")
    HASH_OUT_INITIAL=$(tree_hash "$OUT")
    if ! is_hash "$HASH_MODS_INITIAL" && [ "$HASH_MODS_INITIAL" != AUSENTE ]; then
        echo "FAIL: não consegui hashear mods/$PKG (adb/su sem resposta?) — nada foi tocado"
        rm -rf "$HOST_TMP"
        exit 1
    fi
    if ! is_hash "$HASH_OUT_INITIAL" && [ "$HASH_OUT_INITIAL" != AUSENTE ]; then
        echo "FAIL: não consegui hashear files/bepinex (adb/su sem resposta?) — nada foi tocado"
        rm -rf "$HOST_TMP"
        exit 1
    fi
    echo "  estado inicial: mods=$HASH_MODS_INITIAL files/bepinex=$HASH_OUT_INITIAL"
fi

# Lock de execução concorrente: mkdir é atômico, então dois kits não dividem o
# device. Lock com pid/ts no nome do arquivo dentro; "vivo" = recente, e aí o
# outro run ABORTA. Órfão (run morto) só sai com --force — que NÃO quebra lock
# vivo: dois kits simultâneos dividem STAGE/BAK/MARK de nomes fixos e se
# corrompem (regra da revisão: --force é remédio de órfão, não de concorrência).
lock_take() {
    if [ "$DRY" = 1 ]; then
        echo "DRY> tomaria $LOCK"
        return 0
    fi
    _lp=$$
    _lh=$(hostname 2>/dev/null || echo '?')
    _lt=$(date +%s)
    # mkdir é atômico no device: se o dir já existe, o out loses e ninguém dividiu.
    if dev "mkdir $LOCK 2>/dev/null"; then
        dev "printf 'pid=$_lp\\nhost=$_lh\\nts=$_lt\\n' > $LOCK/ts" || true
        LOCK_HELD=1
        echo "  lock pego: $LOCK (pid $_lp)"
        return 0
    fi
    _lts=$(dev "sed -n 's/^ts=//p' $LOCK/ts 2>/dev/null" | tr -d '\r' | head -1)
    _now=$(date +%s)
    _age=$(( _now - ${_lts:-0} ))
    _live=0
    if [ -n "$_lts" ] && [ "$_age" -ge 0 ] && [ "$_age" -lt "$LOCK_MAX_AGE_S" ]; then
        _live=1
    fi
    if [ "$_live" = 1 ]; then
        echo "FAIL: ja existe um device_test VIVO neste device (lock $LOCK, ha ${_age}s)."
        if [ "$FORCE" = 1 ]; then
            echo "      --force nao quebra lock VIVO. Se aquele run morreu mesmo, apague"
            echo "      $LOCK/ts na mao e rode de novo."
        else
            echo "      Se aquele run morreu, use --force para pegar o lock orfao."
        fi
        return 1
    fi
    if [ "$FORCE" = 1 ]; then
        echo "AVISO: --force: quebrando lock orfao $LOCK (idade ${_age}s)"
        dev "rm -rf $LOCK && mkdir -p $LOCK" || true
        dev "printf 'pid=$_lp\\nhost=$_lh\\nts=$_lt\\n' > $LOCK/ts" || true
        LOCK_HELD=1
        echo "  lock pego: $LOCK (pid $_lp, via --force)"
        return 0
    fi
    echo "FAIL: lock orfao em $LOCK (idade ${_age}s, limite ${LOCK_MAX_AGE_S}s). Use --force para pegar."
    return 1
}

# Só solta o lock quem o pegou: um run que FALHOU ao tomar o lock de outro
# não pode apagá-lo no caminho de saída (e o guard substitui o acidente
# antigo, em que o restore morria em variável não definida ANTES do drop).
lock_drop() {
    [ "$LOCK_HELD" = 1 ] || return 0
    if [ "$DRY" = 1 ]; then return 0; fi
    dev "rm -rf $LOCK" || true
}

# Daqui pra baixo mexemos no device: o trap entra ANTES do backup.
trap restore EXIT
trap 'echo "interrompido — restaurando"; exit 130' INT TERM HUP

if ! lock_take; then
    echo "FAIL: não peguei o lock de execução" >&2
    # Este run não mexeu em nada depois do snapshot (a recuperação de run
    # morto, se rodou, foi ela mesma uma restauração conferida): nada a
    # restaurar — e o lock que sobra é do OUTRO run, que ninguém toca.
    RESTORE_DONE=1
    exit 1
fi

# --- backup de mods/$PKG e files/bepinex -------------------------------------
# (os hashes do estado inicial já foram capturados ANTES do lock: falha de
# leitura ali aborta o run sem tocar nada; o restore confia neles.)
# Backup em .part + mv: se morrer no meio da cópia, o BAK final não existe
# (ou é o do run anterior, já recuperado/consumido) e o restore não pode
# substituir uma pasta boa por uma cópia pela metade.
if [ "$DRY" = 0 ]; then
    dev "rm -rf $STAGE $BAK.part $BAK_OUT.part" || true
    # Backup de MODS: copia para .part, CONFERE que a copia é igual (hash) e só
    # então troca. O `rm -rf $BAK` de dentro só existe depois da prova — um mv
    # direto com destino já existente (orfao de um run anterior) enterraria a copia
    # dentro do backup velho.
    dev "cp -a $MODS $BAK.part" \
        || { echo "FAIL: sem pasta mods/$PKG no device (instale os mods antes)"; exit 1; }
    _baknow=$(tree_hash "$BAK.part")
    _modsnow=$(tree_hash "$MODS")
    if ! is_hash "$_baknow" || ! is_hash "$_modsnow" || [ "$_baknow" != "$_modsnow" ]; then
        echo "FAIL: backup de mods não confere ($_baknow != $_modsnow) — nada foi trocado"
        exit 1
    fi
    dev "rm -rf $BAK && mv $BAK.part $BAK" \
        || { echo "FAIL: não consegui gravar o backup de mods"; exit 1; }
    if dev "test -d $OUT"; then
        dev "cp -a $OUT $BAK_OUT.part" || true
        _bakoutnow=$(tree_hash "$BAK_OUT.part")
        _outnow=$(tree_hash "$OUT")
        if is_hash "$_bakoutnow" && [ "$_bakoutnow" = "$_outnow" ]; then
            dev "rm -rf $BAK_OUT && mv $BAK_OUT.part $BAK_OUT" || true
        else
            echo "AVISO: backup de files/bepinex não confere — o restore vai recusar apagar essa pasta"
            dev "rm -rf $BAK_OUT.part" || true
        fi
    else
        dev "rm -rf $BAK_OUT"
    fi
    # Staging precisa ser gravável pelo shell (adb push roda como uid 2000):
    # cria via su e devolve a ele (2000:2000 + 775). Sem isso o push morre com
    # Permission denied (staging herdava dono root do mkdir via su).
    dev "mkdir -p $STAGE && chown 2000:2000 $STAGE && chmod 775 $STAGE" || true
    # Marcador no DEVICE (o flag do host morre com o run): o próximo run sabe que
    # precisa recuperar antes de mexer. Carrega o sha do snapshot inicial, para
    # alguém conferir na mão o que era o estado "original".
    # sha do snapshot inicial calculado NO HOST (o device não tem o path do host:
    # mandar o sha256sum do arquivo de lá voltaria vazio) e embutido no marcador.
    BEFORE_SHA=$(sha256sum "$BEFORE_FILE" | cut -d' ' -f1)
    echo "  marcador: before_sha=$BEFORE_SHA"
    _mh=$(hostname 2>/dev/null || echo '?')
    _mt=$(date +%s)
    dev "printf 'before_sha=$BEFORE_SHA\\npid=$$\\nhost=$_mh\\nts=$_mt\\n' > $MARK" || true
fi

NEED_PATCH=0
NEED_JS=0
for f in "$TDIR"/*.bpatch "$TDIR"/*.js; do
    [ -e "$f" ] || continue
    case "$f" in *.bpatch) NEED_PATCH=1;; *.js) NEED_JS=1;; esac
done
SKIP_JS=0
if [ "$NEED_JS" = 1 ]; then
    if [ "$NO_FRIDA" = 1 ]; then
        SKIP_JS=1
        echo "SKIP: *.js ignorado (--no-frida)"
    elif ! dev "test -f $MODS/u_frida.so" || ! dev "test -f $MODS/frida-gadget.bin"; then
        SKIP_JS=1
        echo "SKIP: sem u_frida.so/frida-gadget.bin no device — *.js não testado (instale pra cobrir)"
    fi
fi
if [ "$NEED_PATCH" = 1 ]; then
    dev "test -f $MODS/u_patch.so" || { echo "FAIL: u_patch.so ausente em mods/$PKG (instale antes)"; exit 1; }
fi

# Gancho de teste (NÃO usado em produção): T1_SIM_EXIT=<instalado|espera> aborta
# o script naquele ponto sem passar por nenhum cleanup, para o restore-sim.sh
# local provar o caminho de interrupção (artefato instalado + saída != 0 +
# device de volta ao inicial) sem depender de entrega de sinal do shell.
sim_exit() { [ "${T1_SIM_EXIT:-}" = "$1" ] && { echo "SIM: abortando em $1"; exit 9; } || true; }

echo "--- instalando artefatos de $TDIR ---"
for f in "$TDIR"/*.bpatch "$TDIR"/*.conf "$TDIR"/*.js; do
    [ -e "$f" ] || continue
    case "$f" in
        *.js) [ "$SKIP_JS" = 1 ] && continue;;
    esac
    base=$(basename "$f")
    # mods/ é root 755: push vai pro staging (shell escreve), su instala.
    do_adb push "$f" "$STAGE/$base" >/dev/null || { echo "FAIL: push $base"; exit 1; }
    dev "cp $STAGE/$base $MODS/$base && chmod 644 $MODS/$base" || { echo "FAIL: instala $base"; exit 1; }
    # Sem 2>/dev/null aqui de propósito: no dry-run o eco DRY iria pro
    # /dev/null junto (dev() imprime no stderr); no real o barulho do adb
    # só aparece no caminho de aviso mesmo.
    dev "chcon u:object_r:bepinex_mod_file:s0 $MODS/$base" \
        || echo "AVISO: chcon falhou em $base (tipo inexistente? Enforcing pode negar)"
done

sim_exit instalado

if [ "$DRY" = 1 ]; then
    echo "DRY: pularia limpar logs, logcat -c, force-stop, launch, espera de até ${TIMEOUT}s,"
    echo "DRY: checagem de crash, e estas expectativas (WOULD-CHECK):"
    while IFS= read -r re || [ -n "$re" ]; do
        case "$re" in ''|\#*) continue;; esac
        echo "DRY-WOULD-CHECK: $re"
    done < "$EXPECT"
    if [ "$HOLD_AFTER_PASS" -gt 0 ]; then
        echo "DRY: manteria o jogo aberto por ${HOLD_AFTER_PASS}s depois das expectativas para a ação manual"
    fi
    [ "$SKIP_JS" = 0 ] && [ "$NEED_JS" = 1 ] && echo "DRY-WOULD-CHECK: frida_ok.txt + frida_count.txt"
    echo "DRY-RUN OK (nada executado, nada alterado)"
    rm -rf "$HOST_TMP"   # dry-run também limpa o temp do host
    exit 0
fi

dev "rm -f $OUT/log.txt $OUT/frida_ok.txt $OUT/frida_count.txt"
do_adb shell am force-stop "$PKG" >/dev/null 2>&1 || true
# logcat limpo ANTES de subir: sem isso um crash antigo de outra app seria
# cobrado neste teste, e um crash novo pode ser expulso do buffer durante a
# espera (PASS falso).
do_adb logcat -c >/dev/null 2>&1 || true
do_adb shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
PID=""
for _i in 1 2 3 4 5; do
    sleep 3
    PID=$(do_adb shell pidof "$PKG" 2>/dev/null | tr -d '\r' || true)
    [ -n "$PID" ] && break
done
[ -n "$PID" ] || { echo "FAIL: jogo não subiu em ~15s (pid vazio)"; exit 1; }
echo "jogo no ar, pid $PID — esperando até ${TIMEOUT}s"

ELAPSED=0
LOG=""
ALL_OK=0
while [ "$ELAPSED" -lt "$TIMEOUT" ]; do
    LOG=$(dev "cat $OUT/log.txt 2>/dev/null" | tr -d '\r' || true)
    ALL_OK=1
    while IFS= read -r re || [ -n "$re" ]; do
        case "$re" in ''|\#*) continue;; esac
        echo "$LOG" | grep -Eq "$re" || ALL_OK=0
    done < "$EXPECT"
    [ "$ALL_OK" = 1 ] && break
    sim_exit espera
    sleep 3
    ELAPSED=$((ELAPSED + 3))
done

echo "--- expectativas ($EXPECT) ---"
FAIL_N=0
while IFS= read -r re || [ -n "$re" ]; do
    case "$re" in ''|\#*) continue;; esac
    if echo "$LOG" | grep -Eq "$re"; then
        echo "PASS: $re"
    else
        echo "FAIL: $re"
        FAIL_N=$((FAIL_N + 1))
    fi
done < "$EXPECT"

if [ "$FAIL_N" -eq 0 ] && [ "$HOLD_AFTER_PASS" -gt 0 ]; then
    echo "MANUAL: expectativas satisfeitas; faça agora a ação de jogo. Mantendo aberto por ${HOLD_AFTER_PASS}s."
    sleep "$HOLD_AFTER_PASS"
    LOG=$(dev "cat $OUT/log.txt 2>/dev/null" | tr -d '\r' || true)
fi

CRASH=0
NEWPID=$(do_adb shell pidof "$PKG" 2>/dev/null | tr -d '\r' || true)
if [ -z "$NEWPID" ]; then
    echo "CRASH: processo do jogo morreu durante o teste"
    CRASH=1
fi
# Primeiro o PID (o que realmente morreu; logcat --pid existe no Android 8+),
# com fallback por nome para logcat antigo — o nome entra como regex escapado.
FATAL=$(do_adb logcat -d --pid="$PID" 2>/dev/null | tr -d '\r' | grep -E "Fatal signal|FATAL EXCEPTION" || true)
if [ -z "$FATAL" ]; then
    FATAL=$(do_adb logcat -d 2>/dev/null | tr -d '\r' \
        | grep -E "Fatal signal.*$PKG_RE|FATAL EXCEPTION.*$PKG_RE" || true)
fi
if [ -n "$FATAL" ]; then
    echo "CRASH (logcat):"
    echo "$FATAL" | head -5
    CRASH=1
fi

if [ "$NEED_JS" = 1 ] && [ "$SKIP_JS" = 0 ]; then
    if dev "test -f $OUT/frida_ok.txt"; then
        echo "PASS: frida_ok.txt existe"
        dev "cat $OUT/frida_ok.txt" | tr -d '\r'
    else
        echo "FAIL: frida_ok.txt ausente (script não executou)"
        FAIL_N=$((FAIL_N + 1))
    fi
    if dev "test -f $OUT/frida_count.txt"; then
        echo "PASS: frida_count.txt existe"
        dev "cat $OUT/frida_count.txt" | tr -d '\r'
    else
        echo "FAIL: frida_count.txt ausente (intercept não contou)"
        FAIL_N=$((FAIL_N + 1))
    fi
fi

if [ "$CRASH" = 1 ] || [ "$FAIL_N" -gt 0 ]; then
    echo "RESULTADO: FAIL ($FAIL_N falhas, crash=$CRASH)"
    exit 1
fi
echo "RESULTADO: PASS (tudo aplicado, sem crash)"
