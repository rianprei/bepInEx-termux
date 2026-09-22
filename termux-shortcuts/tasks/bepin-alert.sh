#!/data/data/com.termux/files/usr/bin/sh
# bepin-alert.sh — assiste o stream do companion, alerta só em WARN/ERROR/FATAL.
# Roda em paralelo ao viewer/stream normal (não substitui — filtra, não imprime).
#
# --alert-once é crítico: o stream emite uma linha por frame de hook — sem
# alert-once, um WARN repetido tocaria som centenas de vezes por segundo.
# Com --alert-once + --id fixo, só o primeiro alerta daquele id soa.
# Fonte: github.com/termux/termux-api — app/src/main/java/com/termux/api/apis/
# NotificationAPI.java (campos title/content/id/priority/sound/alert-once
# lidos do intent), wiki.termux.com/wiki/Termux-notification.

# Repo pode estar em qualquer path no device — override via env.
# (Default: clone do repo rianprei/bepinEx-termux no $HOME do Termux — nome
# real do slug do GitHub, `git clone` cria a pasta com esse nome exato,
# independente do texto estilizado "bepInEx-termux" usado na prosa/README.)
REPO_DIR="${BEPIN_TERMUX_DIR:-$HOME/bepinEx-termux}"
# achado de review: sem 2>/dev/null aqui pra nao esconder erro real (python3
# ausente, REPO_DIR errado) -- se o comando falhar, o `while read` so recebe
# EOF e o script sai quieto, mas o stderr real fica visivel no log do caller.
python3 "$REPO_DIR/tools/termux_client.py" stream |
while IFS= read -r line; do
    case "$line" in
        *"Warning"*|*"Error"*|*"Fatal"*)
            # achado de review: "${line:0:200}" e' substring expansion
            # bash-only -- o shebang (linha 1) e' sh (dash no Termux), que
            # da "Bad substitution" no 1o alerta. cut e' POSIX, funciona
            # em dash e bash igual.
            content=$(printf '%s' "$line" | cut -c1-200)
            termux-notification --id bepin-warn \
                --title "bepin: alerta" \
                --content "$content" \
                --priority high --sound --alert-once
            ;;
    esac
done
