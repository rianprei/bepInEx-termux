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

python3 "$HOME/battlecats-mods/zygisk-bc-poc/termux_client.py" stream 2>/dev/null |
while IFS= read -r line; do
    case "$line" in
        *"Warning"*|*"Error"*|*"Fatal"*)
            termux-notification --id bepin-warn \
                --title "bepin: alerta" \
                --content "${line:0:200}" \
                --priority high --sound --alert-once
            ;;
    esac
done
