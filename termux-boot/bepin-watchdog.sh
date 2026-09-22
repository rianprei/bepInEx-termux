#!/data/data/com.termux/files/usr/bin/sh
# Roda via Termux:Boot (~/.termux/boot/), precisa chmod +x. Só monitora — não
# levanta o companion: Termux:Boot dispara em BOOT_COMPLETED, antes do jogo
# abrir, então o companion (filho do processo do jogo) ainda não existe.
# Fonte: github.com/termux/termux-boot (BootReceiver.java, BootJobService.java).
# Repo pode estar em qualquer path no device — override via env.
# (Default: clone do repo rianprei/bepinEx-termux no $HOME do Termux — nome
# real do slug do GitHub, `git clone` cria a pasta com esse nome exato,
# independente do texto estilizado "bepInEx-termux" usado na prosa/README.)
REPO_DIR="${BEPIN_TERMUX_DIR:-$HOME/bepinEx-termux}"
termux-wake-lock
# achado de review: sem trap, um kill/crash do script deixava o wake-lock
# preso pra sempre (drena bateria) -- libera em qualquer saida.
trap 'termux-wake-unlock' EXIT INT TERM

# achado de review: logger (util-linux) nao vem por padrao no Termux --
# "command not found" a cada 15s, watchdog parece rodar mas nunca loga
# nada de verdade. log_msg cai pra echo (sempre disponivel) se faltar.
if command -v logger >/dev/null 2>&1; then
    log_msg() { logger -t bepin-watchdog "$1"; }
else
    log_msg() { echo "[bepin-watchdog] $1"; }
fi

# achado de review: python3 e' pacote opt-in no Termux (pkg install
# python) -- sem checar, "command not found" (exit 127) e' lido como
# "companion morto" pra sempre, mesmo com o companion saudavel.
if ! command -v python3 >/dev/null 2>&1; then
    log_msg "python3 nao encontrado -- instale com 'pkg install python' (watchdog nao pode checar o companion sem isso)"
fi

# loop de watchdog: monitora socket do companion, reconecta se cair
while true; do
  if ! command -v python3 >/dev/null 2>&1; then
    sleep 15
    continue
  fi
  # tenta conectar no socket @bc_companion via python client
  if ! python3 "$REPO_DIR/tools/termux_client.py" ping >/dev/null 2>&1; then
    log_msg "companion morto — aguardando jogo relançar"
  else
    log_msg "companion OK"
  fi
  sleep 15
done
