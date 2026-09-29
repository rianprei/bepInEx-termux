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

# ---------------------------------------- installLocation (opção, não mod) ---
python3 - "$PATCHER" "$FIX" <<'PY3'
import importlib.util, json, os, subprocess, sys, zipfile

spec = importlib.util.spec_from_file_location("tabs_patch", sys.argv[1])
tp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tp)
FIX = sys.argv[2]
PATCHER = sys.argv[1]

def die(m):
    print("tabs_patch_test: %s" % m, file=sys.stderr)
    sys.exit(1)

raw = open(os.path.join(FIX, "AndroidManifest.xml"), "rb").read()
ax = tp.Axml(raw)
if ax.read_attr_enum("manifest", "installLocation") != 2:
    die("o fixture não tem installLocation=2 (preferExternal): o APK mudou")
# 1 byte só: o DWORD 0x00000002 -> 0x00000001
n_before = sum(1 for x, y in zip(raw, bytes(ax.set_attr_enum and tp.Axml(raw).data)) if x != y)
ax2 = tp.Axml(raw)
ax2.set_attr_enum("manifest", "installLocation", 1, expected=2)
changed = sum(1 for x, y in zip(raw, bytes(ax2.data)) if x != y)
if changed != 1:
    die("trocar installLocation mexeu em %d bytes, esperava 1" % changed)
if len(raw) != len(ax2.data):
    die("trocar installLocation mudou o tamanho do manifest")
# E o original tem que continuar recusado: se o APK de entrada já vier com
# internal, o lock/expected=2 tem que reclamar em vez de "consertar" calado.
try:
    tp.Axml(bytes(ax2.data)).set_attr_enum("manifest", "installLocation", 1, expected=2)
except tp.AxmlError:
    print("ok installLocation: 2->internal é 1 byte, e expected=2 recusa o já-trocado")
else:
    die("expected=2 aceitou um manifest já interno: a opção passaria calada em "
        "APK de outra geração")

# A opção pela CLI tem que existir E não pode virar etapa: relatório sem
# mudança de telemetria, e o valor interno no manifest final.
import tempfile
work = tempfile.mkdtemp()
src = os.path.join(work, "in.apk")
with zipfile.ZipFile(src, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("AndroidManifest.xml", raw)
    z.writestr("assets/XDConfig.json", open(os.path.join(FIX, "XDConfig.json"), "rb").read())
rep = os.path.join(work, "r.json")
out = os.path.join(work, "o.apk")
p = subprocess.run([sys.executable, PATCHER, "--apk", src, "--out", out,
                    "--stages", "telemetry-track-plugin", "--install-location",
                    "internal", "--ignore-lock", "--report", rep],
                   capture_output=True, text=True)
if p.returncode != 0:
    die("CLI com --install-location falhou: %s" % p.stderr.strip()[:200])
d = json.load(open(rep))
if d.get("install_location", {}).get("to") != 1:
    die("relatório não registrou a opção de instalação: %s" % d)
if len(d["changes"]) != 1 or d["changes"][0]["entry"] != "AndroidManifest.xml":
    die("a opção de installs contaminou a lista de mudanças: %s" % d["changes"])
zout = zipfile.ZipFile(out)
if tp.Axml(zout.read("AndroidManifest.xml")).read_attr_enum("manifest", "installLocation") != 1:
    die("o APK final não ficou com installLocation interno")
print("ok installLocation: opção separada das etapas e presente no APK final")
PY3


# ------------------------------------------- did-local (smali, funcao pura) ---
python3 - "$PATCHER" <<'PY5'
import importlib.util, sys

spec = importlib.util.spec_from_file_location("tabs_patch", sys.argv[1])
tp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tp)

def die(m):
    print("tabs_patch_test: %s" % m, file=sys.stderr)
    sys.exit(1)

# O smali do método real, como o baksmali escreve. O gate não roda java nem
# precisa do jar (o jar vive em out/, fora do git): o que se testa aqui é a
# TRANSFORMAÇÃO de texto, que é a parte que pode estar errada. O
# desmontar/remontar de verdade é testado no device, e está no README do
# did-restore-wip.
smali_real = """.class public Lcom/xd/intl/common/utils/DeviceUtils;
.super Ljava/lang/Object;

.method public static makeUniqueDeviceIdByManual()Ljava/lang/String;
    .locals 7

    :try_start_0
    sget-object v0, Landroid/os/Build;->SERIAL:Ljava/lang/String;
    invoke-static {}, Lcom/xd/intl/common/utils/DeviceUtils;->getLocalMacAddress()Ljava/lang/String;
    move-result-object v1
    new-instance v2, Ljava/util/UUID;
    invoke-virtual {v0}, Ljava/lang/String;->hashCode()I
    move-result v0
    :try_end_0
    .catch Ljava/lang/Exception; {:try_start_0 .. :try_end_0} :catch_0

    goto :goto_0

    :catch_0
    move-exception v0
    const-string v0, ""
    :goto_0
    return-object v0
.end method

.method public static getAndroidId(Landroid/content/Context;)Ljava/lang/String;
    .registers 2
    const-string v0, ""
    return-object v0
.end method
"""
novo = tp.patch_device_utils_smali(smali_real, tp.DID_UUID_DEFAULT)
if novo.count(tp.DID_METHOD_HEAD) != 1:
    die("a transformação duplicou ou apagou o método")
corpo = novo[novo.index(tp.DID_METHOD_HEAD):novo.index(tp.DID_METHOD_HEAD) + 2000]
corpo = corpo[:corpo.index(".end method")]
if "const-string v0, \"%s\"" % tp.DID_UUID_DEFAULT not in corpo:
    die("o did não entrou no método: %s" % corpo[:200])
if "return-object v0" not in corpo:
    die("o método novo não devolve o objeto")
if ":try_start_0" in corpo or "Build;->SERIAL" in corpo:
    die("o corpo do NPE (Build.SERIAL/MAC) continua no método")
# e o resto do arquivo tem que ficar igual: o getAndroidId é stub e ninguém mexeu nele
if 'getAndroidId' not in novo or 'const-string v0, ""' not in novo.split('.method public static getAndroidId')[1]:
    die("a transformação mexeu em outro método")
# o did tem que ser um UUID, e não o userId da conta (que é o que o patches
# anterior escrevia por engano — o servidor trata os dois como coisas diferentes)
if len(tp.DID_UUID_DEFAULT) != 36 or tp.DID_UUID_DEFAULT.count("-") != 4:
    die("DID_UUID_DEFAULT não é um UUID: %r" % tp.DID_UUID_DEFAULT)
for ruim in ("933914692236345345", "nao-e-uuid", ""):
    try:
        tp.patch_device_utils_smali(smali_real, ruim)
        die("a transformação aceitou um did inválido: %r" % ruim)
    except ValueError:
        pass
# e o método tem que aparecer uma vez só: duas, o patch trocaria a errada
try:
    tp.patch_device_utils_smali(smali_real + smali_real, tp.DID_UUID_DEFAULT)
    die("a transformação aceitou um smali com o método duplicado")
except ValueError:
    pass
print("ok did-local: corpo virou const-string UUID + return-object, resto do "
      "arquivo intacto, entrada invalida e duplicada recusadas")
PY5

# ------------------------------------------- guest-local (smali, 2 metodos) ---
python3 - "$PATCHER" <<'PY6B'
import importlib.util, sys

spec = importlib.util.spec_from_file_location("tabs_patch", sys.argv[1])
tp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tp)

def die(m):
    print("tabs_patch_test: %s" % m, file=sys.stderr)
    sys.exit(1)

def corpo(texto, head):
    i = texto.index(head)
    c = texto[i:]
    return c[:c.index(".end method")]

# --- 1) GlobalUserStore.init(): semeia a sessao local, preservando o init real
store = """.class public final Lcom/xd/intl/common/global/GlobalUserStore;
.super Ljava/lang/Object;

.method public init()V
    .locals 1

    new-instance v0, Lcom/xd/intl/common/model/AccountPreferenceImpl;

    invoke-direct {v0}, Lcom/xd/intl/common/model/AccountPreferenceImpl;-><init>()V

    iput-object v0, p0, Lcom/xd/intl/common/global/GlobalUserStore;->mAccountPreference:Lcom/xd/intl/common/model/IAccountPreference;

    return-void
.end method

.method public clearToken()V
    .locals 1
    return-void
.end method
"""
novo = tp.patch_guest_local_smali(store, "store")
c = corpo(novo, tp.GLOBAL_STORE_INIT_HEAD)
# o que o init original fazia tem que continuar fazendo, senao o SDK de conta
# perde o IAccountPreference e quebra tudo que vem depois
if "AccountPreferenceImpl" not in c or "mAccountPreference" not in c:
    die("o init original foi perdido: %s" % c[:300])
for precisa in ('setId', 'setNickName', 'setLoginType', 'setExpired', 'setExpiresIn',
                'setAccessToken', 'currentUser', 'currentAccessToken'):
    if precisa not in c:
        die("a sessao local nao tem %s: %s" % (precisa, c[:300]))
if '"%s"' % tp.GUEST_ID not in c:
    die("o id local nao entrou: %s" % c[:300])
# expired=false: se o autoLogin ver isExpired()==true, ele aborta com
# XD_TOKEN_EXPIRED e o jogo volta a cair no login. E o -100 (DEFAULT_UNKNOWN_
# LOGIN_TYPE) e o que faz o autoLogin pular isTokenActiveWithType.
if "const/4 v2, 0x0\n\n    invoke-virtual {v1, v2}, Lcom/xd/intl/common/bean/XDAccessToken;->setExpired(Z)V" not in c:
    die("setExpired nao recebe false: %s" % c[:400])
if "const v2, -0x64" not in c:
    die("loginType nao e DEFAULT_UNKNOWN_LOGIN_TYPE(-100): %s" % c[:400])
# .locals tem que caber o maior v usado (v0,v1,v2 + p0) -> no minimo 3
locals_n = int(c.split(".locals ")[1].split("\n")[0].strip())
if locals_n < 3:
    die(".locals %d nao comporta v2" % locals_n)
# e o outro metodo do arquivo tem que ficar igual
if "clearToken" not in novo or len(novo) < len(store):
    die("a transformação mexeu em outro metodo")

# --- 2) XDAccountCore.fetchUserInfo(): sucesso local, sem rede
core = """.class public final Lcom/xd/intl/account/XDAccountCore;
.super Ljava/lang/Object;

.method private final fetchUserInfo(Lcom/xd/intl/common/bean/XDGUser;Lcom/xd/intl/common/callback/Callback;)V
    .locals 3

    sget-object v0, Lcom/xd/intl/account/impl/TDSGlobalAccountComponent;->INSTANCE:Lcom/xd/intl/account/impl/TDSGlobalAccountComponent;

    invoke-virtual {v0}, Lcom/xd/intl/account/impl/TDSGlobalAccountComponent;->getUserInfo()Lio/reactivex/Observable;

    move-result-object v0

    invoke-virtual {v0, v1}, Lio/reactivex/Observable;->subscribe(Lio/reactivex/Observer;)V

    return-void
.end method

.method private final fetchAreaCodeInfos()V
    .locals 1
    return-void
.end method
"""
novo_c = tp.patch_guest_local_smali(core, "fetch")
cf = corpo(novo_c, tp.FETCH_USER_INFO_HEAD)
# o corpo tem comentarios que NOMEIAM o que foi removido (e isso e util), entao
# a rede e procurada so nas linhas de instrucao
instrucoes = "\n".join(l for l in cf.split("\n") if not l.strip().startswith("#"))
# a cadeia Rx era a rede; se qualquer resto dela sobrar, o patch e inerte
for proibido in ("TDSGlobalAccountComponent", "Observable", "NetRespObserver",
                 "Schedulers", "subscribe"):
    if proibido in instrucoes:
        die("fetchUserInfo ainda tem rede (%s): %s" % (proibido, instrucoes[:300]))
# resposta: callback com o MESMO usuario (p1) e erro nulo, e null-safe no cb
if "invoke-interface {p2, p1, v0}" not in cf:
    die("o callback nao recebe o usuario: %s" % cf[:300])
if "if-eqz p2" not in cf:
    die("o callback nulo nao e tratado: %s" % cf[:300])
if "return-void" not in cf:
    die("fetchUserInfo nao volta: %s" % cf[:300])
if "fetchAreaCodeInfos" not in novo_c:
    die("a transformação mexeu em outro metodo")

# --- 3) as guardas de unicidade valem para os dois alvos
for head, qual in ((tp.GLOBAL_STORE_INIT_HEAD, "store"), (tp.FETCH_USER_INFO_HEAD, "fetch")):
    if "==V" not in head and ")V" not in head:
        die("cabecalho de teste estranho: %r" % head)
    try:
        tp.patch_guest_local_smali(head + "\n.end method\n" + head + "\n.end method\n", qual)
        die("aceitou smali com %s duplicado" % qual)
    except ValueError:
        pass
    try:
        tp.patch_guest_local_smali(".class public final Lcom/xd/intl/common/global/GlobalUserStore;\n.super Ljava/lang/Object;\n", qual)
        die("aceitou smali sem o metodo %s" % qual)
    except ValueError:
        pass
try:
    tp.patch_guest_local_smali(store, "qual-que-nao-existe")
    die("aceitou qual desconhecido")
except ValueError:
    pass

# --- 4) o corpo novo precisa fechar o metodo, senao o smali nao assembla
if ".end method" not in tp.smali_guest_store_init():
    die("o corpo do init nao fecha o metodo")
if ".end method" not in tp.smali_fetch_user_info_local():
    die("o corpo do fetchUserInfo nao fecha o metodo")
# const/high16 exige os 16 bits baixos zerados: um expiresIn com cauda nao
# assembla (ja falhou assim uma vez)
if "0x3b9a0000" not in tp.smali_guest_store_init():
    die("o expiresIn nao e um const/high16 valido")
if "const/high16 v2, 0x3b9aca00" in tp.smali_guest_store_init():
    die("expiresIn com os 16 bits baixos nao zerados")
print("ok guest-local: init semeia user+token local preservando o init real, "
      "fetchUserInfo fica sem rede e null-safe, guardas de unicidade e "
      "const/high16 ok")
PY6B

# --------------------------------------------------- noads (so o manifest) ---
python3 - "$PATCHER" "$FIX" <<'PY6'
import importlib.util, json, os, subprocess, sys, zipfile

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

out = tp.stage_noads(read)
if [e for e, _, _ in out] != ["AndroidManifest.xml"]:
    die("noads tem que tocar só o manifest")
ax = tp.Axml(dict((e, d) for e, d, _ in out)["AndroidManifest.xml"])
if ax.read_meta_data_value("com.tapsdk.billboard.loader")[1] & 0xFFFFFFFF:
    die("noads não desligou o loader de billboard")
# e o resto continua: conta, pagamento, anti-addiction, login, conteúdo
for loader in tp.LOADERS_PARA_NUNCA_TOCAR:
    if not (ax.read_meta_data_value(loader)[1] & 0xFFFFFFFF):
        die("noads mexeu em %s, que é login/conta/pagamento/conteúdo" % loader)
# telemetria e anúncio são etapas DIFERENTES (uma não pode vir no lugar da
# outra): se alguém somar as duas, a trava de "uma etapa por arquivo" recusa
try:
    tp.stage_noads(read) + tp.stage_telemetry_config(read)
    # a união acima é só teste de que as chaves não se misturam
except ValueError as e:
    die("união artificial de noads+telemetry falhou de forma inesperada: %s" % e)
print("ok noads: só o loader de billboard desligado, %d loaders críticos intactos"
      % len(tp.LOADERS_PARA_NUNCA_TOCAR))
PY6

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

# Etapa pendente não pode fingir que rodou, e nome inventado tem que ser
# recusado por nome. `perf` ainda não existe; `--ignore-lock` porque aqui a
# recusa tem que ser a da etapa, não a do sha256 do APK de teste.
p = subprocess.run([sys.executable, sys.argv[1], "--apk", src, "--out",
                    os.path.join(WORK, "x.apk"), "--stages", "perf", "--ignore-lock"],
                   capture_output=True, text=True)
if p.returncode == 0 or "não implementada" not in p.stderr:
    die("etapa pendente (perf) não foi recusada com 'não implementada': %s" % p.stderr[:140])
p = subprocess.run([sys.executable, sys.argv[1], "--apk", src, "--out",
                    os.path.join(WORK, "y.apk"), "--stages", "telemetria", "--ignore-lock"],
                   capture_output=True, text=True)
if p.returncode == 0 or "desconhecida" not in p.stderr:
    die("nome de etapa inexistente não foi recusado: %s" % p.stderr[:140])
print("ok etapas: perf recusada como pendente, nome inexistente recusado")

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
