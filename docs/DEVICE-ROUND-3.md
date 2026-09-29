# Rodada 3 no aparelho — smoke checks pós-rodada-2

Roteiro para validar no POCO C75/Android 16 os caminhos ainda sem evidência
de aparelho. **Este arquivo é um roteiro, não um resultado**: nenhuma etapa
abaixo foi executada no telefone ao ser escrita.

## Limites e preflight

- Não instalar/atualizar APK, módulo Magisk/KernelSU ou jogo nesta rodada.
  Não tocar em APK, OBB ou save. Usar apenas os builds já instalados.
- Não trocar SELinux, não aplicar regra `magiskpolicy` ao vivo, não reiniciar o
  telefone. Registrar o estado original e testar em `getenforce` sem alterá-lo.
- As únicas gravações permitidas são fixtures temporárias em Download, na pasta
  temporária de mods e no diretório `files/bepinex/`; cada uma tem remoção
  explícita abaixo. Se o destino já existir, **pare**: não sobrescreva.
- Uma dependência ausente, build incompatível ou device sem o ABI/jogo
  necessário é **BLOQUEADO**, nunca PASS nem evidência de regressão.
- Não execute `toggle_mod`, `set_mod`, `unpatch_mod`, `repatch_mod` ou
  `reload_config` nesta rodada: eles alteram estado de hooks/config. Os 12
  verbos continuam cobertos pelo teste de host no gate; o smoke físico abaixo
  usa apenas `ping`, `status` e o fixture inerte de `push_mod`.
- Gadget/Frida executa código no processo do jogo e pode derrubá-lo. Use
  somente o script read-only desta rodada e o gadget pinado 17.18.0. Não use
  scripts de terceiros nem teste o gadget 17.19.0.
- Nos smokes que abrem um jogo, não avance telas nem interaja com gameplay;
  se o jogo iniciar uma sessão jogável automaticamente, encerre o teste sem
  PASS. Não leia nem escreva APK/OBB/save diretamente; se o app anunciar
  atualização, restauração ou salvamento automático, interrompa a etapa.

No host, configure os pacotes, guarde os logs atuais e confira conexão/root:

```bash
PKG_SA2=com.hyperdotstudios.swampattack2
PKG_BC=jp.co.ponos.battlecatsen
OUT=/tmp/device-round3
test ! -e "$OUT/logcat-before.txt" && test ! -L "$OUT/logcat-before.txt"
mkdir -p "$OUT"
adb devices -l
adb shell su -c id
adb shell getenforce
adb shell getprop ro.product.cpu.abilist
adb shell dumpsys package "$PKG_SA2" | grep -E 'versionName|primaryCpuAbi|secondaryCpuAbi'
adb logcat -d -b all -v threadtime > "$OUT/logcat-before.txt"
```

Confirme que os pacotes/builds já instalados correspondem ao teste, que a
versão do gadget é 17.18.0, e anote `getenforce`. Não instale um APK para
corrigir uma pré-condição ausente.

## Ordem de execução

As etapas vão de observação sem mutação a execução de código no jogo. Termine
uma etapa e faça a limpeza indicada antes de avançar.

### 1. Manager — mensagem acionável para ABI incompatível (risco baixo)

**Pré-requisitos:** Manager já instalado; `PKG_SA2` é detectado como
`arm64-v8a`; o fixture ARM32 versionado existe; o nome de teste ainda não está
em Download nem na pasta de mods.

```bash
adb shell su -c "test ! -e /data/local/tmp/mods/$PKG_SA2/round3-abi-probe.so \
    && test ! -L /data/local/tmp/mods/$PKG_SA2/round3-abi-probe.so"
adb shell "test ! -e /sdcard/Download/round3-abi-probe.so \
    && test ! -L /sdcard/Download/round3-abi-probe.so"
adb push test/fixtures/modtypes/mod_arm32.so /sdcard/Download/round3-abi-probe.so
```

No Manager: abra SA2 → **+ Instalar mod** → selecione
`round3-abi-probe.so`. A recusa deve ocorrer antes de copiar qualquer arquivo.
Isto é texto de diálogo, não uma linha de `logcat`:

- `ABI incompatível: o jogo usa arm64-v8a, mas o mod usa armeabi-v7a. Baixe a versão arm64-v8a do mod.` — `manager/src/io/github/rianprei/bepinex/manager/core/NativeAbiDetector.java:87-88` (anchor: `Baixe a versão `).
- O diálogo usa o título `Não instalado` e o prefixo de erro `Falha ao instalar o arquivo: ` — `manager/src/io/github/rianprei/bepinex/manager/GameDetailActivity.java:287-292` (anchor: `"Falha ao instalar o arquivo: "`).

**PASS:** a mensagem completa acima aparece e
`/data/local/tmp/mods/$PKG_SA2/round3-abi-probe.so` continua ausente.
Mensagem genérica, instalação aceita ou arquivo copiado = **FAIL**.

```bash
adb shell rm -f /sdcard/Download/round3-abi-probe.so
adb shell su -c "test ! -e /data/local/tmp/mods/$PKG_SA2/round3-abi-probe.so"
```

### 2. Cliente Termux — socket e respostas read-only (risco baixo)

**Pré-requisitos:** cliente e `push_mod_emit.py` já instalados pelo instalador
do projeto no Termux; companion ativo; não reinstalar nada durante esta etapa.
No app Termux:

```bash
cd ~/battlecats-mods/zygisk-bc-poc
python3 termux_client.py ping
python3 termux_client.py status
```

Respostas literais do companion (a saída do cliente é resposta de protocolo,
não `logcat`):

- `pong` — `jni/companion.cpp:781-781` (anchor: `"pong"`).
- `companion_active` — `jni/companion.cpp:806-806` (anchor: `"companion_active"`).

**PASS:** cada comando termina com status 0 e imprime exatamente a resposta
correspondente, sem `error:`/timeout. Qualquer outra resposta, traceback,
timeout ou ausência do companion = **FAIL**. Não rode os verbos que alteram
config ou hooks; eles não são necessários para provar a conexão neste smoke.

### 3. Símbolos + `push_mod_emit` — transferência inerte (risco baixo/moderado)

Este teste envia uma biblioteca mínima cujo único entry point retorna
`false`; o loader deve descartá-la como inativa, sem instalar hooks. A
biblioteca de saída é stripped, mantém o build-id do original e é removida
antes de terminar. **Não use um mod real como fixture.** As saídas temporárias
e o índice de símbolos abaixo também precisam estar ausentes antes de começar;
o teste não os sobrescreve nem remove dados de uma execução anterior.

**Pré-requisitos:** device e SA2 não estão rodando; ABI de Battle Cats é
`arm64-v8a`; NDK configurado; Termux conectado; os seguintes destinos estão
ausentes: `/sdcard/Download/round3-noop.so` e
`/data/local/tmp/bc_mods/round3-noop.so`.

Antes de gerar a fixture, confirme que os dois destinos remotos estão ausentes:

```bash
adb shell "test ! -e /sdcard/Download/round3-noop.so \
    && test ! -L /sdcard/Download/round3-noop.so"
adb shell su -c "test ! -e /data/local/tmp/bc_mods/round3-noop.so \
    && test ! -L /data/local/tmp/bc_mods/round3-noop.so"
```

No host, na raiz do checkout:

```bash
set -euo pipefail
NDK_BIN="${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}/toolchains/llvm/prebuilt/linux-x86_64/bin"
CLANG="$NDK_BIN/aarch64-linux-android26-clang"
test -x "$CLANG"
test ! -e /tmp/device-round3-noop.c && test ! -L /tmp/device-round3-noop.c
test ! -e /tmp/device-round3-noop.unstripped.so && test ! -L /tmp/device-round3-noop.unstripped.so
test ! -e /tmp/device-round3-noop.so && test ! -L /tmp/device-round3-noop.so
test ! -e /tmp/device-round3-symbols && test ! -L /tmp/device-round3-symbols
cat > /tmp/device-round3-noop.c <<'EOF'
#include "bc_mod_api.h"
BC_MOD_EXPORT bool bc_mod_register(const bc_mod_api *api) {
    (void)api;
    return false;
}
EOF
. tools/symbols.sh
"$CLANG" -std=c11 -fPIC -shared -g -I jni -Wl,--build-id=sha1 \
    -o /tmp/device-round3-noop.unstripped.so /tmp/device-round3-noop.c
symbols_ship /tmp/device-round3-noop.unstripped.so /tmp/device-round3-noop.so \
    /tmp/device-round3-symbols
test "$(symbols_build_id /tmp/device-round3-noop.unstripped.so)" = \
     "$(symbols_build_id /tmp/device-round3-noop.so)"
SECTIONS="$("$(symbols_readelf)" -S /tmp/device-round3-noop.so)"
if printf '%s\n' "$SECTIONS" | grep -q '\.symtab'; then
    echo "FAIL: stripped probe still contains .symtab" >&2
    exit 1
fi
adb push /tmp/device-round3-noop.so /sdcard/Download/round3-noop.so
```

No Termux:

```bash
cd ~/battlecats-mods/zygisk-bc-poc
python3 termux_client.py push_mod /sdcard/Download/round3-noop.so
```

A resposta e o log de entrega são:

- `ok: %ld bytes written` (o `%ld` é o tamanho real da fixture) —
  `jni/companion.cpp:723-724` (anchor: `"ok: %ld bytes written\n"`).
- `push_mod: wrote %s (%ld bytes)` (caminho e tamanho reais) —
  `jni/companion.cpp:726-726` (anchor: `"push_mod: wrote %s (%ld bytes)"`).

Depois abra Battle Cats e confira:

```bash
adb shell monkey -p "$PKG_BC" 1
sleep 5
adb logcat -d -b all -v threadtime | grep -F \
  'mod loader: round3-noop.so carregado mas inativo (entry retornou false)'
REMOTE_SIZE="$(adb shell su -c 'wc -c < /data/local/tmp/bc_mods/round3-noop.so' | tr -d '\r')"
LOCAL_SIZE="$(wc -c < /tmp/device-round3-noop.so | tr -d ' ')"
test "$REMOTE_SIZE" = "$LOCAL_SIZE"
```

O resultado esperado do loader é `mod loader: %s carregado mas inativo (entry retornou false)` — `jni/main.cpp:1612-1612` (anchor: `"mod loader: %s carregado mas inativo (entry retornou false)"`).

**PASS:** resposta e log de entrega presentes; o tamanho no device é igual ao
`wc -c` do `.so` stripped no host; build-id de origem e destino iguais;
`.symtab` ausente no arquivo enviado; o log do loader diz **inativo**; jogo
continua vivo. Tamanho divergente, build-id divergente, `.symtab` no arquivo,
ausência de uma linha, crash ou estado ativo = **FAIL**.

```bash
adb shell am force-stop "$PKG_BC"
adb shell su -c 'rm -f /data/local/tmp/bc_mods/round3-noop.so'
adb shell rm -f /sdcard/Download/round3-noop.so
rm -rf /tmp/device-round3-symbols /tmp/device-round3-noop.c \
    /tmp/device-round3-noop.unstripped.so /tmp/device-round3-noop.so
```

Remova somente esses nomes de teste. Não apague a pasta de mods nem o
diretório de símbolos do usuário.

### 4. Mono mínimo (F13) — bloqueado, não executável nesta base

O item ainda não integra uma implementação Mono ao loader nem abre um
assembly. Portanto não há comando de device que valide carga de mod Mono, e
nenhuma linha de runtime pode ser declarada como esperada. `docs/ROADMAP-UNIVERSAL.md:302-302` (anchor: `Falta: integrar o runtime ao loader`).

**Resultado desta rodada:** **BLOQUEADO**, não PASS. Não tente carregar DLL
Mono, não copie assemblies para o jogo e não infira suporte a partir de
`libmono.so` presente em `/proc/<pid>/maps`. Reabrir esta etapa somente quando
um entry point do loader, abertura de assembly e fixture real estiverem
integrados e tiverem mensagem-fonte citável.

### 5. Guards ARM32 — executar só num jogo ARM32 já instalado (risco moderado)

**Pré-requisitos:** não instale jogo/APK. Use somente um jogo já instalado
cujo `primaryCpuAbi` seja `armeabi-v7a`, com os builds ARM32 atuais de
`sa2ammo`, `sa2content`, `u_patch` e `u_noads` já presentes. Confirme:

```bash
adb shell dumpsys package "$PKG_SA2" | grep -E 'primaryCpuAbi=armeabi-v7a'
```

Se não houver correspondência, esta etapa é **BLOQUEADA**: não tente forçar
execução ARM32 num processo ARM64. Se houver, inicie captura de logcat, abra o
jogo uma vez e procure as recusas literais:

- `não suportado em 32-bit: o hook Dobby é AArch64 e não foi validado em aparelho 32-bit; nenhum hook foi instalado` — `mods/sa2ammo/jni/sa2ammo_mod.cpp:67-68` (anchor: `"não suportado em 32-bit: o hook Dobby é AArch64`).
- A mesma recusa para `sa2content` — `mods/sa2content/jni/sa2content_mod.cpp:205-206` (anchor: `"não suportado em 32-bit: o hook Dobby é AArch64`).
- `não suportado em 32-bit: o emissor C4 gera instruções AArch64; nenhuma regra foi aplicada` — `mods/u_patch/jni/u_patch_mod.cpp:795-796` (anchor: `"não suportado em 32-bit: o emissor C4 gera instruções AArch64;`).
- `não suportado em 32-bit: a guarda de prólogo e o hook são AArch64; nenhum hook foi instalado` — `mods/u_noads/jni/u_noads_mod.cpp:169-170` (anchor: `"não suportado em 32-bit: a guarda de prólogo e o hook são AArch64;`).
- O log do detector genérico não pertence ao teste dos quatro mods acima.
  Para testá-lo separadamente, use apenas um app ARM32 já instalado sem pasta
  `/data/local/tmp/mods/$PKG_GENERIC_ARM32`; se não houver, marque esse
  subteste **BLOQUEADO**. Confirme ABI e pasta ausente, abra o app uma vez e
  espere `detector/hook genérico ainda não suportado em 32-bit; ignorando` —
  `jni/main.cpp:2192-2193` (anchor: `detector/hook genérico ainda não suportado em 32-bit; ignorando`).

```bash
adb shell dumpsys package "$PKG_GENERIC_ARM32" | grep -F 'primaryCpuAbi=armeabi-v7a'
adb shell su -c "test ! -e /data/local/tmp/mods/$PKG_GENERIC_ARM32 \
    && test ! -L /data/local/tmp/mods/$PKG_GENERIC_ARM32"
adb shell monkey -p "$PKG_GENERIC_ARM32" 1
adb logcat -d -b all -v threadtime | grep -F \
  'detector/hook genérico ainda não suportado em 32-bit; ignorando'
```
- `u_frida` só participa se a configuração e o gadget ARM64 já estiverem
  presentes; não instale o gadget para criar a pré-condição. Se a ABI divergir,
  a recusa é `%s não corresponde à ABI do processo — gadget NAO carregado` —
  `mods/u_frida/jni/u_frida_mod.cpp:106-106` (anchor: `"%s não corresponde à ABI do processo — gadget NAO carregado"`).

**PASS:** processo confirmado ARM32, todos os quatro mods instalados emitem a
respectiva recusa, nenhum hook/regra do grupo é instalado, e o PID do jogo
continua vivo por 30 segundos. Mod ausente = **BLOQUEADO**; linha ausente,
hook inesperado, crash ou processo não ARM32 = **FAIL** (exceto ABI não
disponível, que continua bloqueado). Não mude ABI nem instale outra cópia do
jogo para fabricar um PASS.

### 6. Bridge `Il2Cpp.perform` com Frida 17.18.0 (risco alto; executar por último)

**Pré-requisitos:** `PKG_SA2` permanece exatamente
`com.hyperdotstudios.swampattack2`; `u_frida.so` já está instalado; o gadget
pinado é 17.18.0; SA2 é IL2CPP; estes nomes não existem ainda na pasta de mods nem em
`files/bepinex/`: `round3_il2cpp_bridge.js`,
`frida-gadget.bin`, `frida-gadget.config` e
`round3_il2cpp_bridge.txt`. O staging
`/data/local/tmp/frida-stage-$PKG_SA2` e o bundle
`/tmp/device-round3/round3_il2cpp_bridge.js` também precisam estar ausentes
(incluindo symlinks quebrados).
A pasta de mods já deve estar em modo `755` e label
`u:object_r:bepinex_mod_file:s0`; `deploy_frida.sh` reaplica esses atributos e
remove o staging. Verifique tudo antes; se algum destino existir ou os
atributos forem diferentes, pare sem executar o instalador. A listagem precisa
mostrar `drwxr-xr-x` e `u:object_r:bepinex_mod_file:s0`.

Gere o bundle da bridge versionada e prepare os arquivos temporários:

```bash
test ! -e /tmp/device-round3/round3_il2cpp_bridge.js \
    && test ! -L /tmp/device-round3/round3_il2cpp_bridge.js
mkdir -p /tmp/device-round3
adb shell su -c "test ! -e /data/local/tmp/frida-stage-$PKG_SA2 \
    && test ! -L /data/local/tmp/frida-stage-$PKG_SA2"
adb shell su -c "test ! -e /data/local/tmp/mods/$PKG_SA2/round3_il2cpp_bridge.js \
    && test ! -L /data/local/tmp/mods/$PKG_SA2/round3_il2cpp_bridge.js \
    && test ! -e /data/local/tmp/mods/$PKG_SA2/frida-gadget.bin \
    && test ! -L /data/local/tmp/mods/$PKG_SA2/frida-gadget.bin \
    && test ! -e /data/local/tmp/mods/$PKG_SA2/frida-gadget.config \
    && test ! -L /data/local/tmp/mods/$PKG_SA2/frida-gadget.config \
    && test ! -e /data/data/$PKG_SA2/files/bepinex/round3_il2cpp_bridge.txt \
    && test ! -L /data/data/$PKG_SA2/files/bepinex/round3_il2cpp_bridge.txt"
adb shell su -c "ls -ldZ /data/local/tmp/mods/$PKG_SA2"
tools/bundle_frida_script.sh \
    test/device/sa2/round3_il2cpp_bridge_smoke.js \
    /tmp/device-round3/round3_il2cpp_bridge.js
tools/deploy_frida.sh "$PKG_SA2" /tmp/device-round3/round3_il2cpp_bridge.js
adb logcat -c
adb shell am force-stop "$PKG_SA2"
adb shell monkey -p "$PKG_SA2" 1
sleep 15
adb logcat -d -b all -v threadtime | grep -E 'IL2CPP pronto em até 10s; carregando gadget|gadget ativo'
adb shell su -c "grep -Fx 'Il2Cpp.perform round3 ok' /data/data/$PKG_SA2/files/bepinex/round3_il2cpp_bridge.txt"
```

O marker é escrito somente depois de `Il2Cpp.perform` — a fixture não chama
API do jogo nem modifica save: `test/device/sa2/round3_il2cpp_bridge_smoke.js:4-5` (anchor: `"Il2Cpp.perform round3 ok\n"`).
As mensagens de boot/carga vêm do código:

- `IL2CPP pronto em até 10s; carregando gadget` — `mods/u_frida/jni/u_frida_mod.cpp:130-130` (anchor: `"IL2CPP pronto em até 10s; carregando gadget"`).
- `gadget ativo (type=%s, pasta %s, 1º script %s)` (valores preenchidos pelo processo) — `mods/u_frida/jni/u_frida_mod.cpp:140-140` (anchor: `"gadget ativo (type=%s, pasta %s, 1º script %s)"`).

**PASS:** ambas as mensagens aparecem, o marker contém exatamente
`Il2Cpp.perform round3 ok`, o jogo permanece vivo e não há AVC relacionado.
Ausência da mensagem/marker ou qualquer `avc: denied` envolvendo o domínio do
processo SA2, `bepinex_mod_file`, ou caminhos desta etapa = **FAIL**; tombstone,
SIGSEGV ou ANR = **FAIL e parar**, sem repetir. Não adicione regra SELinux por
teoria.

Após coletar as evidências, force-stop e remova somente o que esta etapa criou:

```bash
adb shell am force-stop "$PKG_SA2"
adb shell su -c "rm -f /data/local/tmp/mods/$PKG_SA2/round3_il2cpp_bridge.js \
    /data/local/tmp/mods/$PKG_SA2/frida-gadget.bin \
    /data/local/tmp/mods/$PKG_SA2/frida-gadget.config \
    /data/data/$PKG_SA2/files/bepinex/round3_il2cpp_bridge.txt \
    && rm -rf /data/local/tmp/frida-stage-$PKG_SA2"
rm -f /tmp/device-round3/round3_il2cpp_bridge.js
```

### 7–9. Mods-reloc — **após merge e revisão cruzada**

Na base `03eac87`, a árvore root-only/entrega por FD e o canal de pedidos
Battle Cats ainda constam como fora da base; estes itens não podem ser
executados nem receber PASS antes do merge: `docs/ROADMAP-UNIVERSAL.md:50-53` (anchor: `árvore root-only + entrega por FD (mods-reloc) | em revisão, fora da base`).

Depois do merge de toda a cadeia e da revisão cruzada, incluir aqui três
etapas separadas, todas com o novo kit de snapshot/restore:

1. **REQ pós-specialize via `@bc_companion`:** lançar apenas o jogo já
   instalado e conferir a linha que confirma carga de mods por FD no caminho
   REQ; comparar o PID do jogo antes/depois e falhar em crash/timeout.
2. **SELinux `connectto`:** repetir em Enforcing sem mudar policy; aprovar
   somente se a comunicação jogo→companion funciona e não há
   `avc: denied { connectto }` relevante com `permissive=0`. Coletar o AVC
   integral se houver; nunca aplicar `magiskpolicy` durante este roteiro.
3. **Mods Battle Cats pelo canal:** usar fixture inerte, comparar bytes do
   FD recebido, confirmar que o jogo BC carrega somente o mod permitido e que
   jogo genérico não vê/recebe essa árvore; restaurar snapshot e conferir
   hashes.

**Status agora: BLOQUEADO.** Os comandos definitivos, mensagens literais
`arquivo:linha`, e critérios por retorno ainda dependem do commit integrado;
preenchê-los a partir de `03eac87` seria citar comportamento ausente. Até a
atualização deste bloco, não execute nem marque qualquer dos três como PASS.

## Coleta de falhas e encerramento

Após qualquer falha, antes da limpeza:

```bash
adb logcat -d -b all -v threadtime > "$OUT/logcat-all.txt"
adb logcat -d -b all -v threadtime | grep -E \
  'avc: denied|FATAL EXCEPTION|SIGSEGV|SIGABRT|ANR|bepinex|u_frida|u_patch|u_noads|sa2ammo|sa2content|companion|mod loader|push_mod' \
  > "$OUT/logcat-filtered.txt"
adb shell su -c 'dmesg | grep -i "avc: denied"' > "$OUT/avc-denied.txt" 2>&1 || true
adb shell su -c "cat /data/data/$PKG_SA2/files/bepinex/log.txt" \
  > "$OUT/sa2-bepinex-log.txt" 2>&1 || true
```

Para SIGSEGV/ANR, preserve também o tombstone correspondente e informe qual
etapa, ABI, modo SELinux e hash do build instalado. Para AVC, preserve a linha
inteira (`scontext`, `tcontext`, `tclass`, permissão, caminho e
`permissive=`); não resuma o domínio nem sugira uma regra antes da evidência.
No fim, confira que fixtures temporárias estão ausentes. Não limpe logs,
mods, configs ou estado preexistente além dos nomes de teste listados.
