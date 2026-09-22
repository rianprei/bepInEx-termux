#!/data/data/com.termux/files/usr/bin/sh
# Roda via Termux:Boot (~/.termux/boot/), precisa chmod +x. Só monitora — não
# levanta o companion: Termux:Boot dispara em BOOT_COMPLETED, antes do jogo
# abrir, então o companion (filho do processo do jogo) ainda não existe.
# Fonte: github.com/termux/termux-boot (BootReceiver.java, BootJobService.java).
# Repo pode estar em qualquer path no device — override via env.
# (Default: clone do repo rianprei/bepinEx-termux no $HOME do Termux.)
REPO_DIR="${BEPIN_TERMUX_DIR:-$HOME/bepinEx-termux}"
termux-wake-lock
# loop de watchdog: monitora socket do companion, reconecta se cair
while true; do
  # tenta conectar no socket @bc_companion via python client
  if ! python3 "$REPO_DIR/tools/termux_client.py" ping >/dev/null 2>&1; then
    logger -t bepin-watchdog "companion morto — aguardando jogo relançar"
  else
    logger -t bepin-watchdog "companion OK"
  fi
  sleep 15
done
