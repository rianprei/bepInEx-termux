#!/usr/bin/env bash
# test/tabs_patch_test.sh — o patcher do APK do TABS (etapa `telemetry`) faz o
# que diz, sobre os bytes REAIS do manifest e do config do jogo.
#
# O que este teste segura, e por que cada afirmação existe:
#
#   (1) db_config.enable vira false e NADA MAIS no config. Um patch que
#       reescreve o arquivo inteiro (json round-trip) mudaria a indentação
#       dele, e o diff do APK pararia de mostrar uma linha. Comparação byte a
#       byte, não "o JSON está correto".
#   (2) Os dois meta-data de telemetria (com.tapsdk.tapdb.loader e
#       com.xd.third.track.loader) viram false, e os OITO loaders de
#       login/conta/pagamento/momento/conquista/anúncio/share/anti-addiction
#       continuam true. A lista de nunca-tocar é a rede de segurança contra
#       alguém "aproveitar" a etapa telemetria para matar o login.
#   (3) O manifest patched tem o mesmo tamanho e difere em 8 bytes: dois
#       DWORD de dados. Patch que mexe em mais que isso (ou reescreve a pool
#       de strings) está fazendo mais do que promete.
#   (4) A surgery de zip copia as entradas não tocadas byte a byte, troca só a
#       entrada pedida, e o resultado continua legível por zipfile. APK de
#       1,4 GB reescrito por zipfile (decompress + recompress) seria lento e
#       mudaria a compressão de libil2cpp.so; o teste prova que não é assim.
#   (5) O lock é conferido: um APK de entrada com sha256 diferente é recusado
#       com mensagem, e só passa com --ignore-lock. Patcher que aceita
#       qualquer entrada é como o dia vira joke: o patch "passa" num APK de
#       outra versão e o jogador recebe um jogo quebrado.
#   (6) SABOTAGEM (controle negativo): uma cópia do patcher com a edição do
#       db_config removida é executada contra o mesmo checker, e o checker
#       tem que reprovar. Sem isto, (1) seria uma afirmação que só prova que
#       o teste rodou.
#
# Node/sem rede: o gate não baixa nada. Os fixtures são os dois arquivos reais
# de 28 KB e 1 KB.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
PATCHER="$ROOT/tools/tabs_patch.py"
FIX="$ROOT/test/fixtures/tabs"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

die() { echo "tabs_patch_test: $*" >&2; exit 1; }
ok() { echo "tabs_patch_test: $*"; }

[ -f "$PATCHER" ] || die "patcher ausente: $PATCHER"
[ -f "$FIX/AndroidManifest.xml" ] || die "fixture ausente: AndroidManifest.xml"
[ -f "$FIX/XDConfig.json" ] || die "fixture ausente: XDConfig.json"
command -v python3 >/dev/null 2>&1 || die "python3 não está no host: o patcher é stdlib-only e o gate exige python3"

# ---------------------------------------------------------------- 1, 2, 3 ---
python3 - "$PATCHER" "$FIX" <<'PY'
import importlib.util, json, os, sys

spec = importlib.util.spec_from_file_location("tabs_patch", sys.argv[1])
tp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tp)

def die(m):
    print("tabs_patch_test: %s" % m, file=sys.stderr)
    sys.exit(1)

def read(name):
    # O fixture do config é salvo pelo nome do asset, e o patcher pede o
    # caminho dentro do APK (assets/XDConfig.json).
    local = os.path.basename(name)
    with open("%s/%s" % (sys.argv[2], local), "rb") as f:
        return f.read()

orig_cfg = read("XDConfig.json")
orig_manifest = read("AndroidManifest.xml")

changes = tp.stage_telemetry(read)
out = {}
for entry, data, why in changes:
    if entry in out:
        die("etapa telemetry devolveu %s duas vezes" % entry)
    out[entry] = data

# (1) config: uma linha, e a linha é a do interruptor.
cfg = out["assets/XDConfig.json"].decode()
old_lines = orig_cfg.decode().split("\n")
new_lines = cfg.split("\n")
if len(old_lines) != len(new_lines):
    die("config mudou de tamanho em linhas: %d -> %d (o patch tem que ser "
        "uma linha, não um reformat)" % (len(old_lines), len(new_lines)))
changed = [(i, a, b) for i, (a, b) in enumerate(zip(old_lines, new_lines)) if a != b]
if len(changed) != 1:
    die("config: %d linhas alteradas, esperava 1" % len(changed))
i, a, b = changed[0]
if '"enable"' not in a or "true" not in a or '"enable"' not in b or "false" not in b:
    die("a linha alterada não é a do enable: %r -> %r" % (a, b))
# E o config tem que continuar sendo JSON válido, com o resto intacto.
obj = json.loads(cfg)
if obj["tapsdk"]["db_config"]["enable"] is not False:
    die("db_config.enable não é false depois do patch")
if obj["tapsdk"]["client_id"] != "u0m5lrcpyznrbe7pc7" or obj["tapsdk"]["client_token"] == "":
    die("patch mexeu em client_id/client_token: login depende deles")
if obj["tapsdk"]["server_url"] != "https://u0m5lrcp.cloud.ap-sg.tapapis.com":
    die("patch mexeu em server_url; isso é do login/config e é medida à parte")
if len(obj["tapsdk"]["permissions"]) != 2:
    die("patch mexeu em permissions")
print("ok config: 1 linha, enable true->false, resto do JSON intacto")

# (2) manifest: telemetria desligada, allow-list intacta.
manifest = out["AndroidManifest.xml"]
ax = tp.Axml(manifest)
for loader in tp.TELEMETRY_LOADERS:
    t, v = ax.read_meta_data_value(loader)
    if t != 0x12:
        die("%s: android.value virou tipo 0x%02x, esperava booleano" % (loader, t))
    if v & 0xFFFFFFFF:
        die("%s continua true: telemetria não desligada" % loader)
for loader in tp.LOADERS_PARA_NUNCA_TOCAR:
    t, v = ax.read_meta_data_value(loader)
    if not (v & 0xFFFFFFFF):
        die("%s virou false: a etapa telemetria tocou algo que é login, "
            "conta, pagamento, conteúdo ou anúncio" % loader)
print("ok manifest: %d loaders de telemetria false, %d loaders críticos true"
      % (len(tp.TELEMETRY_LOADERS), len(tp.LOADERS_PARA_NUNCA_TOCAR)))

# (3) surgicalidade: mesmos bytes de tamanho, 8 bytes de diferença.
if len(manifest) != len(orig_manifest):
    die("manifest mudou de tamanho: %d -> %d" % (len(orig_manifest), len(manifest)))
diff = [k for k, (x, y) in enumerate(zip(orig_manifest, manifest)) if x != y]
if len(diff) != 8:
    die("manifest difere em %d bytes, esperava 8 (dois DWORD de dados)" % len(diff))
print("ok manifest: mesmo tamanho, 8 bytes diferentes")

# Reaplicar tem que reclamar, não fingir: o segundo patch encontraria os
# loaders já falsos e o config já false.
try:
    tp.stage_telemetry(lambda name: manifest if name == "AndroidManifest.xml" else cfg.encode())
except Exception as e:
    print("ok idempotência: reaplicar falha com motivo (%s)" % str(e)[:60])
else:
    die("reaplicar a etapa no APK já patchado passou: o patcher não sabe "
        "dizer que a entrada não é a esperada")
PY

# ------------------------------------------------- metades isoladas (1b) ---
python3 - "$PATCHER" "$FIX" <<'PY2'
import importlib.util, json, os, sys

spec = importlib.util.spec_from_file_location("tabs_patch", sys.argv[1])
tp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tp)
FIX = sys.argv[2]

def die(m):
    print("tabs_patch_test: %s" % m, file=sys.stderr)
    sys.exit(1)

def read(name):
    with open(os.path.join(FIX, os.path.basename(name)), "rb") as f:
        return f.read()

# Cada metade toca UM arquivo só. É o que permite medir no device qual das
# duas o jogo aceita: se as duas viessem juntas, um login quebrado não diria
# qual delas foi a culpada.
cfg_only = tp.stage_telemetry_config(read)
plug_only = tp.stage_telemetry_plugins(read)
if [e for e, _, _ in cfg_only] != ["assets/XDConfig.json"]:
    die("telemetry-config tem que tocar só o config; tocou %s"
        % [e for e, _, _ in cfg_only])
if [e for e, _, _ in plug_only] != ["AndroidManifest.xml"]:
    die("telemetry-plugins tem que tocar só o manifest; tocou %s"
        % [e for e, _, _ in plug_only])
if json.loads(dict((e, d) for e, d, _ in cfg_only)["assets/XDConfig.json"])["tapsdk"]["db_config"]["enable"] is not False:
    die("telemetry-config não desligou o db_config")
ax = tp.Axml(dict((e, d) for e, d, _ in plug_only)["AndroidManifest.xml"])
for loader in tp.TELEMETRY_LOADERS:
    if ax.read_meta_data_value(loader)[1] & 0xFFFFFFFF:
        die("telemetry-plugins não desligou %s" % loader)
# E a união tem que continuar tocando os dois (é o que o gate de device mediu
# como quebrando o login;_union_ não pode virar um no-op silencioso).
track = tp.stage_telemetry_track_plugin(read)
if [e for e, _, _ in track] != ["AndroidManifest.xml"]:
    die("telemetry-track-plugin tem que tocar só o manifest")
tax = tp.Axml(dict((e, d) for e, d, _ in track)["AndroidManifest.xml"])
if tax.read_meta_data_value("com.xd.third.track.loader")[1] & 0xFFFFFFFF:
    die("telemetry-track-plugin não desligou o loader de atribuição")
# E o que ela NÃO pode desligar: o TapDB, que é quem gera o did da conta. É
# a diferença entre "telemetria" e "login quebrado" e o device mediu isso.
if not (tax.read_meta_data_value("com.tapsdk.tapdb.loader")[1] & 0xFFFFFFFF):
    die("telemetry-track-plugin desligou o loader do TapDB: isso tira o did "
        "da conta e quebra o login (medido no device)")
union = tp.stage_telemetry(read)
if sorted(e for e, _, _ in union) != ["AndroidManifest.xml", "assets/XDConfig.json"]:
    die("a união telemetry tem que tocar os dois arquivos; tocou %s"
        % [e for e, _, _ in union])
print("ok metades: config toca 1, plugins toca 1, track-plugin só o "
      "atribuição (TapDB fica true), união toca os 2")
PY2

# --------------------------------------------------------------- 4, 5, 6 ---
python3 - "$PATCHER" "$FIX" "$WORK" <<'PY'
import hashlib, importlib.util, json, os, subprocess, sys, zipfile

spec = importlib.util.spec_from_file_location("tabs_patch", sys.argv[1])
tp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tp)
FIX, WORK = sys.argv[2], sys.argv[3]

def die(m):
    print("tabs_patch_test: %s" % m, file=sys.stderr)
    sys.exit(1)

# APK sintético com a MESMA forma de entrada: um XML de manifest qualquer,
# o config real e um "lib" grande que não pode ser recomprimido.
src = os.path.join(WORK, "in.apk")
with zipfile.ZipFile(src, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("AndroidManifest.xml", open(os.path.join(FIX, "AndroidManifest.xml"), "rb").read())
    z.writestr("assets/XDConfig.json", open(os.path.join(FIX, "XDConfig.json"), "rb").read())
    z.writestr("lib/arm64-v8a/libfake.so", os.urandom(300000))

# (4) surgery: entrada trocada tem o conteúdo novo, as outras vão byte a byte.
rep = {"assets/XDConfig.json": b'{"tapsdk": {"db_config": {"enable": false}}}\n'}
dst = os.path.join(WORK, "out.apk")
stats = tp.rewrite_zip(src, dst, rep)
zin, zout = zipfile.ZipFile(src), zipfile.ZipFile(dst)
if sorted(zin.namelist()) != sorted(zout.namelist()):
    die("a lista de entradas mudou")
if zout.read("assets/XDConfig.json") != rep["assets/XDConfig.json"]:
    die("a entrada trocada não tem o conteúdo novo")
for name in ("AndroidManifest.xml", "lib/arm64-v8a/libfake.so"):
    a = zin.getinfo(name)
    b = zout.getinfo(name)
    if (a.compress_size, a.CRC, a.compress_type) != (b.compress_size, b.CRC, b.compress_type):
        die("%s foi recomprimido: a surgery tem que copiar byte a byte o que "
            "não é tocado (libil2cpp.so tem 105 MB)" % name)
    if zin.read(name) != zout.read(name):
        die("%s mudou de conteúdo" % name)
if zout.testzip() is not None:
    die("zipfile diz que a saída tem entrada corrompida")
print("ok surgery: %d entradas, trocada 1, as outras 2 byte a byte" % stats["entries"])

# (5) lock: entrada com sha256 fora do lock é recusada, com motivo.
report = os.path.join(WORK, "report.json")
p = subprocess.run([sys.executable, sys.argv[1], "--apk", src, "--out",
                    os.path.join(WORK, "locked.apk"), "--stages", "telemetry",
                    "--report", report],
                   capture_output=True, text=True)
if p.returncode == 0:
    die("o patcher aceitou um APK fora do lock (sha256 conferido e recusado)")
if "tabs_apk.lock" not in p.stderr or "sha256" not in p.stderr:
    die("recusa sem motivo legível: %s" % p.stderr.strip()[:200])
print("ok lock: entrada fora do lock recusada com motivo")
p = subprocess.run([sys.executable, sys.argv[1], "--apk", src, "--out",
                    os.path.join(WORK, "forced.apk"), "--stages", "telemetry",
                    "--ignore-lock", "--report", report], capture_output=True, text=True)
if p.returncode != 0:
    die("--ignore-lock não deixou passar: %s" % p.stderr.strip()[:200])
rep = json.load(open(report))
if len(rep["changes"]) != 2 or rep["lock_mismatch_ignored"] is not True:
    die("relatório não registrou as 2 mudanças nem o lock pulado: %s" % rep)
for c in rep["changes"]:
    if c["sha256_before"] == c["sha256_after"]:
        die("relatório diz que %s não mudou" % c["entry"])
print("ok relatório: 2 mudanças com sha256 antes/depois")

# Etapa inexistente não pode ser "--stages noads" fingindo que rodou: hoje
# noads/perf são pendentes e o patcher tem que dizer isso.
p = subprocess.run([sys.executable, sys.argv[1], "--apk", src, "--out",
                    os.path.join(WORK, "x.apk"), "--stages", "noads"],
                   capture_output=True, text=True)
if p.returncode == 0 or "não implementada" not in p.stderr:
    die("etapa pendente não foi recusada com 'não implementada': %s" % p.stderr[:120])
print("ok etapas: pendente recusada por nome")

# (6) SABOTAGEM: patcher sem a edição do db_config tem que reprovar no (1).
mutant = os.path.join(WORK, "mutant.py")
src_text = open(sys.argv[1], encoding="utf-8").read()
broken = src_text.replace('    new_text = _set_tapsdk_db_enable(raw.decode("utf-8"), False)\n',
                          '    new_text = raw.decode("utf-8")  # SABOTAGEM: edição removida\n')
if broken == src_text:
    die("a sabotagem não entrou: a linha '_set_tapsdk_db_enable(text, False)' "
        "não existe mais no patcher, então o controle negativo está medindo "
        "outra coisa")
open(mutant, "w", encoding="utf-8").write(broken)
mspec = importlib.util.spec_from_file_location("tabs_patch_mutant", mutant)
mt = importlib.util.module_from_spec(mspec)
mspec.loader.exec_module(mt)
mf = os.path.join(WORK, "mfix")
os.makedirs(mf, exist_ok=True)
for n in ("AndroidManifest.xml", "XDConfig.json"):
    with open(os.path.join(FIX, n), "rb") as f, open(os.path.join(mf, n), "wb") as g:
        g.write(f.read())
mout = dict((e, d) for e, d, _ in mt.stage_telemetry(
    lambda name: open(os.path.join(mf, os.path.basename(name)), "rb").read()))
cfg_mut = mout["assets/XDConfig.json"].decode()
mobj = json.loads(cfg_mut)
if mobj["tapsdk"]["db_config"]["enable"] is not False:
    print("ok sabotagem: o mutante deixou enable=%s e o checker do (1) "
          "reprovaria nele" % mobj["tapsdk"]["db_config"]["enable"])
else:
    die("o mutante produziu enable=false: a sabotagem não sabotou nada, e o "
        "controle negativo está mentindo")
PY

echo "tabs_patch_test: OK"
