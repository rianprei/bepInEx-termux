#!/bin/sh
# T1 kit de validação no device. QUEM EXECUTA É O USUÁRIO (agente não usa adb):
#   tools/device_test.sh <pkg> <dir-de-teste> [timeout_s] [--no-frida] [--dry-run] [--force]
#   (flags sempre DEPOIS dos dois posicionais)
#
# Faz: recuperação de run interrompido → snapshot inicial (lista+sha256 de
# mods/<pkg>/ E de files/bepinex INTEIRA) → backup dos dois → marca run em
# andamento no device → instala artefatos via staging+su → limpa logs →
# logcat -c → abre o jogo → espera as linhas de expect.txt no log.txt →
# checa crash (pid morto / Fatal signal do PID) → PASS/FAIL → RESTAURA TUDO e
# CONFERE (snapshot depois tem de ser idêntico ao inicial).
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

PKG=${1:?uso: tools/device_test.sh <pkg> <dir-de-teste> [timeout_s] [--no-frida] [--dry-run]}
TDIR=${2:?uso: tools/device_test.sh <pkg> <dir-de-teste> [timeout_s] [--no-frida] [--dry-run]}
shift 2
TIMEOUT=120
NO_FRIDA=0
DRY=0
FORCE=0
for a in "$@"; do
    case "$a" in
        --no-frida) NO_FRIDA=1;;
        --dry-run) DRY=1;;
        --force) FORCE=1;;
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
OUT="/data/data/$PKG/files/bepinex"
EXPECT="$TDIR/expect.txt"
HOST_TMP="$(mktemp -d)"
RESTORE_DONE=0
# PKG como regex: ponto literal (com.foo não casa com comXfoo nem com com.foobar)
PKG_RE=$(printf '%s' "$PKG" | sed 's/[.[\*^$()+?{|]/\\&/g')

# Todos os toques no device passam daqui (dry-run imprime no stderr pra não
# poluir capturas $(...)).
# ENTREGA VIA STDIN DE PROPÓSITO: `adb shell su -c "$cmd"` junta os args com
# espaço e o sh do device re-divide — `su -c test -d X && rm ...` vira su
# rodando só `test` e o resto como shell sem root (achado no device:
# chmod/rm/mv com Permission denied). Via stdin o comando chega intacto,
# com && e aspas (prova em test/device/quoting-check.sh).
dev() {
    if [ "$DRY" = 1 ]; then echo "DRY> printf '%s' | adb shell su : $1" >&2; else printf '%s\n' "$1" | adb shell su; fi
}
do_adb() {
    if [ "$DRY" = 1 ]; then echo "DRY> adb $*" >&2; else adb "$@"; fi
}

# Estado canônico: lista + modo + sha256 de TUDO dentro de mods/<pkg>/ e de
# files/bepinex/ (inclusive arquivos que o teste cria: crashguard, dump.tsv,
# log.txt.t1bak...). Sem Pastas próprias, sem wildcard solto.
snapshot_to() {
    dev "for d in $MODS $OUT; do
        if [ -d \$d ]; then
            stat -c '%n %a %U:%G' \$d
            find \$d -type f 2>/dev/null | sort | while read -r f; do stat -c '%n %s %a' \"\$f\"; sha256sum \"\$f\"; done
        else
            echo \"\$d AUSENTE\"
        fi
    done" > "$1" 2>/dev/null
}

# Hash do CONTEÚDO de uma pasta, independente do caminho (nomes relativos),
# para provar que um backup é mesmo cópia do que estava lá. Vazio = não existe.
tree_hash() {
    dev "if [ -d '$1' ]; then cd '$1' && { stat -c '%n %a %U:%G' .; find . -type f 2>/dev/null | sort | while read -r f; do stat -c '%n %s %a' \"\$f\"; sha256sum \"\$f\"; done; } | sha256sum | cut -d' ' -f1; fi" 2>/dev/null | tr -d '\r' | grep -E '^[0-9a-f]{64}$' || true
}

# Restaura UM árbol, e só apaga depois de provar que o backup é o original.
# Sem backup verificado: NÃO APAGA NADA e devolve 1 (o restore vira exit 2, e o
# marcador fica de pé para o próximo run recuperar). Nada de rm de dado do
# usuário sem backup correspondente conferido — era o que apagava o
# files/bepinex inteiro (dump.tsv do u_dump) quando o BAK_OUT não existia.
restore_one() {
    _what=$1 _live=$2 _bak=$3 _hash=$4
    [ -n "$_hash" ] || { echo "  $_what: sem estado inicial registrado, nada a restaurar"; return 1; }
    if [ -z "$_hash" ] || [ "$_hash" = "AUSENTE" ]; then
        # não existia antes do teste: o que apareceu é nosso, sai
        dev "rm -rf $_live" || return 1
        return 0
    fi
    _bakh=$(tree_hash "$_bak")
    if [ -z "$_bakh" ]; then
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
    restore_one "mods/$PKG" "$MODS" "$BAK" "$HASH_MODS_INITIAL" || RC_TREE=1
    restore_one "files/bepinex" "$OUT" "$BAK_OUT" "$HASH_OUT_INITIAL" || RC_TREE=1
    # staging e .part vão sempre (são nossos); o marcador e os backups SÓ com
    # as duas árvores de volta no lugar — senão o próximo run não tem como
    # recuperar.
    if [ "$RC_TREE" = 0 ]; then
        dev "rm -rf $STAGE $BAK.part $BAK_OUT.part $BAK_OUT" || true
        dev "rm -f $MARK" || true
    else
        echo "  aviso: marcador e backup mantidos para o próximo run recuperar"
    fi
    do_adb shell am force-stop "$PKG" >/dev/null 2>&1 || true
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
    snapshot_to "$HOST_TMP/after.txt"
    if [ ! -s "$HOST_TMP/before.txt" ]; then
        echo "RESTAURACAO FALHOU (snapshot inicial vazio — nada conferível)"
        rm -rf "$HOST_TMP"
        exit 2
    fi
    if [ ! -s "$HOST_TMP/after.txt" ]; then
        echo "RESTAURACAO FALHOU (snapshot pós-restauração vazio — adb sem resposta?)"
        rm -rf "$HOST_TMP"
        exit 2
    fi
    if [ "$TREE_OK" = 1 ] && diff -q "$HOST_TMP/before.txt" "$HOST_TMP/after.txt" >/dev/null 2>&1; then
        echo "device restaurado (mods + files/bepinex idênticos ao inicial)"
        rm -rf "$HOST_TMP"
        return 0
    fi
    echo "RESTAURACAO FALHOU (estado difere do inicial; ver diff)"
    diff "$HOST_TMP/before.txt" "$HOST_TMP/after.txt" 2>/dev/null | head -20 || true
    rm -rf "$HOST_TMP"
    exit 2
}

# --- precondições (sem trap ainda: falhar aqui não é "restauração falhou") ---
[ -d "$TDIR" ] || { echo "FAIL: dir de teste ausente: $TDIR"; exit 1; }
[ -f "$EXPECT" ] || { echo "FAIL: sem expect.txt em $TDIR"; exit 1; }
if [ "$DRY" = 0 ]; then
    command -v adb >/dev/null 2>&1 || { echo "FAIL: adb não encontrado"; exit 1; }
    [ "$(adb get-state 2>/dev/null)" = "device" ] || { echo "FAIL: device não conectado"; exit 1; }
    adb shell su -c true 2>/dev/null || { echo "FAIL: su sem resposta (root?)"; exit 1; }
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
if [ "$DRY" = 0 ]; then
    if dev "test -f $MARK || test -d $BAK"; then
        echo "AVISO: run anterior interrompido detectado (marcador ou backup em $BAK)"
        echo "       recuperando os mods do backup antes de seguir"
        # Recupera o que der: cada árvore só é tocada com backup verificado
        # (tree_hash confere). MODS volta do BAK; OUT só volta se o BAK_OUT
        # existir (senão é o dump.tsv do usuário e NÃO SE APAGA).
        _rmods=$(tree_hash "$BAK")
        _rout=$(tree_hash "$BAK_OUT")
        if [ -n "$_rmods" ]; then
            dev "rm -rf $MODS && cp -a $BAK $MODS" || true
            _rnow=$(tree_hash "$MODS")
            if [ -n "$_rnow" ] && [ "$_rnow" = "$_rmods" ]; then
                # restaurado e conferido: o backup orfao virou redundante
                dev "rm -rf $BAK" || true
                echo "       mods restaurados do backup (hash conferido), backup consumido"
            else
                echo "       AVISO: mods restaurados mas o hash nao bate — backup mantido em $BAK"
            fi
        else
            echo "       AVISO: sem backup de mods em $BAK — nada apagado"
        fi
        if [ -n "$_rout" ]; then
            dev "rm -rf $OUT && cp -a $BAK_OUT $OUT" || true
            echo "       files/bepinex restaurado do backup"
        elif [ ! -d "$OUT" ]; then
            echo "       files/bepinex nao existe (nada a restaurar)"
        else
            echo "       AVISO: sem backup de files/bepinex em $BAK_OUT — pasta do usuario INTACTA"
        fi
        dev "rm -rf $STAGE $BAK.part $BAK_OUT.part" || true
        dev "rm -f $MARK" || true
        echo "       recuperado. Se o dump abaixo mostrar .patch/.conf/.js em mods/<pkg>/, o run"
        echo "       morreu ANTES do marcador e esses arquivos eram do usuário mesmo."
    fi
fi

echo "--- snapshot inicial (mods + files/bepinex) ---"
snapshot_to "$HOST_TMP/before.txt"
if [ "$DRY" = 0 ] && [ ! -s "$HOST_TMP/before.txt" ]; then
    echo "FAIL: snapshot inicial vazio (adb sem resposta? pasta ausente?)"
    rm -rf "$HOST_TMP"
    exit 1
fi
if [ "$DRY" = 0 ]; then
    echo "  $(wc -l < "$HOST_TMP/before.txt" | tr -d ' ') linhas de estado registradas"
fi

# Lock de execução concorrente: mkdir é atômico, então dois kits não dividem o
# device. Lock com pid/ts no nome do arquivo dentro; "vivo" = recente, e aí o
# outro run ABORTA. Órfão (run morto) só sai com --force.
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
        echo "  lock pego: $LOCK (pid $_lp)"
        return 0
    fi
    _lts=$(dev "sed -n 's/^ts=//p' $LOCK/ts 2>/dev/null" | tr -d '\r' | head -1)
    _now=$(date +%s)
    _age=$(( _now - ${_lts:-0} ))
    if [ "$FORCE" = 1 ]; then
        echo "AVISO: --force: quebrando lock $LOCK (idade ${_age}s)"
        dev "rm -rf $LOCK && mkdir -p $LOCK" || true
        dev "printf 'pid=$_lp\\nhost=$_lh\\nts=$_lt\\n' > $LOCK/ts" || true
        return 0
    fi
    if [ -n "$_lts" ] && [ "$_age" -ge 0 ] && [ "$_age" -lt "$LOCK_MAX_AGE_S" ]; then
        echo "FAIL: ja existe um device_test rodando neste device (lock $LOCK, ha ${_age}s)."
        echo "      Se aquele run morreu, use --force para pegar o lock orfao."
        return 1
    fi
    echo "FAIL: lock orfao em $LOCK (idade ${_age}s, limite ${LOCK_MAX_AGE_S}s). Use --force para pegar."
    return 1
}

lock_drop() {
    if [ "$DRY" = 1 ]; then return 0; fi
    dev "rm -rf $LOCK" || true
}

# Daqui pra baixo mexemos no device: o trap entra ANTES do backup.
trap restore EXIT
trap 'echo "interrompido — restaurando"; exit 130' INT TERM HUP

if ! lock_take; then
    echo "FAIL: não peguei o lock de execução" >&2
    exit 1
fi

# Hashes por árvore, do estado INICIAL: o restore só apaga uma árvore se o
# backup dela bater com este hash (restore_one).
# Em dry-run nada é copiado, então não há hash para conferir: o caminho seco
# só mostra o que faria.
if [ "$DRY" = 1 ]; then
    echo "--- (dry-run) faria backup de $MODS e de $OUT em .part+rename, conferiria o hash de cada, e gravaria o marcador $MARK ---"
    HASH_MODS_INITIAL=DRY
    HASH_OUT_INITIAL=DRY
else
    HASH_MODS_INITIAL=$(tree_hash "$MODS")
    HASH_OUT_INITIAL=$(tree_hash "$OUT")
    [ -n "$HASH_MODS_INITIAL" ] || HASH_MODS_INITIAL=AUSENTE
    [ -n "$HASH_OUT_INITIAL" ] || HASH_OUT_INITIAL=AUSENTE
    echo "  estado inicial: mods=$HASH_MODS_INITIAL files/bepinex=$HASH_OUT_INITIAL"

    echo "--- backup de mods/$PKG e files/bepinex ---"
    # Backup em .part + mv: se morrer no meio da cópia, o BAK final não existe
    # (ou é o do run anterior, já recuperado) e o restore não pode substituir uma
    # pasta boa por uma cópia pela metade.
    dev "rm -rf $STAGE $BAK.part $BAK_OUT.part" || true
    # Backup de MODS: copia para .part, CONFERE que a copia é igual (hash) e só
    # então troca. O `rm -rf $BAK` de dentro só existe depois da prova — um mv
    # direto com destino já existente (orfao de um run anterior) enterraria a copia
    # dentro do backup velho.
    dev "cp -a $MODS $BAK.part" \
        || { echo "FAIL: sem pasta mods/$PKG no device (instale os mods antes)"; exit 1; }
    _baknow=$(tree_hash "$BAK.part")
    _modsnow=$(tree_hash "$MODS")
    if [ -z "$_baknow" ] || [ "$_baknow" != "$_modsnow" ]; then
        echo "FAIL: backup de mods não confere ($_baknow != $_modsnow) — nada foi trocado"
        exit 1
    fi
    dev "rm -rf $BAK && mv $BAK.part $BAK" \
        || { echo "FAIL: não consegui gravar o backup de mods"; exit 1; }
    if dev "test -d $OUT"; then
        dev "cp -a $OUT $BAK_OUT.part" || true
        _bakoutnow=$(tree_hash "$BAK_OUT.part")
        _outnow=$(tree_hash "$OUT")
        if [ -n "$_bakoutnow" ] && [ "$_bakoutnow" = "$_outnow" ]; then
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
    if [ "$DRY" = 0 ]; then
        BEFORE_SHA=$(sha256sum "$HOST_TMP/before.txt" | cut -d' ' -f1)
        echo "  marcador: before_sha=$BEFORE_SHA"
        _mh=$(hostname 2>/dev/null || echo '?')
        _mt=$(date +%s)
        dev "printf 'before_sha=$BEFORE_SHA\\npid=$$\\nhost=$_mh\\nts=$_mt\\n' > $MARK" || true
    fi
fi

NEED_PATCH=0
NEED_JS=0
for f in "$TDIR"/*.patch "$TDIR"/*.js; do
    [ -e "$f" ] || continue
    case "$f" in *.patch) NEED_PATCH=1;; *.js) NEED_JS=1;; esac
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
for f in "$TDIR"/*.patch "$TDIR"/*.conf "$TDIR"/*.js; do
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
