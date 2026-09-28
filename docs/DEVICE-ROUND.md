# Roteiro da rodada no device

Sessão única, com o usuário desbloqueando o PIN apenas quando o Android pedir.
Estimativa: **45–60 min** (10 min de soak incluídos; F4/u_patch e Frida podem
acrescentar 10 min se o teste do método precisar ser repetido).

## Antes de começar

- Bateria ≥60%, cabo USB, `adb devices` autorizado e root Magisk/KernelSU
  funcionando (`adb shell su -c id`).
- Fazer backup do save do SA2 antes de instalar ou testar qualquer mod.
- Ter no PC a release `out/release/v0.4.1/`, o pacote
  `com.hyperdotstudios.swampattack2`, o PIN do aparelho e, para o teste de
  Frida, um `.js` de smoke que escreva uma marca em
  `/data/data/<pkg>/files/bepinex/`.
- Guardar os valores originais de `getenforce`, a lista de mods e o conteúdo
  de `/data/data/<pkg>/files/bepinex/`; não apagar save nem APK do jogo.
- Preparar uma cópia local dos artefatos para restaurar, nunca editar o APK do
  jogo.

## Preparação e reboot único

```bash
PKG=com.hyperdotstudios.swampattack2
adb wait-for-device
adb shell su -c id
adb shell getenforce
adb shell su -c "mkdir -p /data/local/tmp/round-backup/$PKG"
adb shell su -c "cp -a /data/local/tmp/mods/$PKG /data/local/tmp/round-backup/$PKG/mods 2>/dev/null || true"
adb shell su -c "cp -a /data/data/$PKG/files/bepinex /data/local/tmp/round-backup/$PKG/state 2>/dev/null || true"
adb install -r out/release/v0.4.1/bepinex-manager-v0.4.1.apk
adb push out/release/v0.4.1/bepinex-termux-v0.4.1.zip /data/local/tmp/
adb shell su -c "magisk --install-module /data/local/tmp/bepinex-termux-v0.4.1.zip"
adb reboot
adb wait-for-device
```

Instalar o zip pelo app Magisk é equivalente se `magisk --install-module` não
estiver disponível. Confirmar no log após o reboot:

```bash
adb shell su -c "grep -E 'módulo carregado|mods/<pkg>/ presente|instalação concluída' /data/data/$PKG/files/bepinex/log.txt"
```

Fontes: `módulo carregado — %s` (`jni/main.cpp:2176`),
`mods/<pkg>/ presente — carga por FD no canal REQ (postAppSpecialize)` (`jni/main.cpp:2292`),
`instalação concluída — state: %s` (`jni/main.cpp:1714`). Restaurar desinstalando o módulo pelo Magisk e
recolocando a cópia de `/data/local/tmp/round-backup/$PKG`.

## Permissive e Enforcing

Executar os blocos de teste abaixo primeiro com:

```bash
adb shell su -c "setenforce 0"
adb shell getenforce                 # esperado: Permissive
```

Depois repetir apenas os testes marcados **[REBOOT]** com:

```bash
adb shell su -c "setenforce 1"
adb shell getenforce                 # esperado: Enforcing
adb logcat -c
adb reboot && adb wait-for-device
```

Em ambos os modos, coletar:

```bash
adb logcat -d -b all | grep -E 'avc: denied|bepinex|u_dump|u_patch|u_frida|u_noads'
adb shell su -c "cat /data/data/$PKG/files/bepinex/log.txt"
```

Não adicionar permissão por teoria: guardar cada `avc: denied` com
`permissive=0` para decidir o execmod de F11/F12. Restaurar no fim para o
valor original salvo antes da rodada.

## F2 — SDK, template e ferramentas

```bash
tools/new_mod.sh hello
tools/deploy_mod.sh hello "$PKG"
adb shell am force-stop "$PKG"
adb shell monkey -p "$PKG" 1
adb shell su -c "grep -E 'carregado, esperando libil2cpp|il2cpp ok|il2cpp não subiu' /data/data/$PKG/files/bepinex/log.txt"
```

Esperado: `carregado, esperando libil2cpp.so` (`mods/_template/jni/mod.cpp:20-27`)
ou `il2cpp ok` (`mods/_template/jni/mod.cpp:20-27`). Confirmar que o processo abre e que a
linha aparece em `log.txt`. Restaurar com:

```bash
adb shell su -c "rm -f /data/local/tmp/mods/$PKG/hello.so"
adb shell am force-stop "$PKG"
```

`tools/pack_bmod.sh hello` deve gerar o pacote quando houver um manifest e
payload válidos; não distribuir `.bpatch` enquanto F4 não estiver na base
(`docs/BMOD-FORMAT.md:100-103`).

## F4 — u_patch: return, mul e field [REBOOT]

Quando `u_patch` entrar pela worktree `f4-upatch`, instalar o zip que contém o
mod e usar a UI/arquivo gerado:

```bash
adb shell su -c "cp /data/local/tmp/round-backup/$PKG/u_patch.so /data/local/tmp/mods/$PKG/u_patch.so"
adb shell su -c "printf '%s\n' 'return ComplexCreature HasAmmo 0 bool true' > /data/local/tmp/mods/$PKG/sa2-field.bpatch"
adb shell am force-stop "$PKG"; adb shell monkey -p "$PKG" 1
adb shell su -c "grep -E 'patch|return|mul|field|aplic' /data/data/$PKG/files/bepinex/log.txt"
```

Para `return`, `mul` e `field`, usar exatamente as linhas emitidas pelo
`u_patch` após o merge; não inventar uma mensagem antes de o código existir.
Confirmar no SA2: `return` mantém munição, `mul` altera o retorno e `field`
mantém o campo na chamada seguinte. Restaurar:

```bash
adb shell su -c "rm -f /data/local/tmp/mods/$PKG/sa2-field.bpatch /data/local/tmp/mods/$PKG/u_patch.so"
adb shell am force-stop "$PKG"
```

O teste automatizado `test/device/sa2-field` deve ser usado quando entrar na
base; repetir em Permissive e Enforcing.

## F7 — módulo Magisk/KernelSU [REBOOT]

Após instalar/reiniciar o zip, confirmar:

```bash
adb shell su -c "ls -lZ /data/adb/modules/*bepinex*/zygisk/arm64-v8a.so"
adb shell su -c "test -f /data/adb/modules/bc-poc/module.prop && cat /data/adb/modules/bc-poc/module.prop"
adb shell su -c "magisk --path 2>/dev/null || true"
```

Esperado no runtime: `módulo carregado — %s` (`jni/main.cpp:2176`).
Confirmar o Manager no launcher e abrir SA2. Para
KernelSU, instalar o mesmo zip pelo app KernelSU + ZygiskNext e repetir o
reboot. Restaurar removendo o módulo pelo app correspondente e reiniciando.

## F5 — Manager

### Status, jogos, tela do jogo e log

Abrir o Manager, confirmar `Root: OK`, `Módulo: OK`, `Zygisk: OK` e a versão
0.4.1. `Root: OK` (`manager/src/io/github/rianprei/bepinex/manager/MainActivity.java:211-222`);
`Manager: ` (`manager/src/io/github/rianprei/bepinex/manager/MainActivity.java:211-222`).
Selecionar SA2
e testar **Reiniciar jogo**, switch do mod e **Ver log**. A tela de log deve
mostrar `/data/data/$PKG/files/bepinex/log.txt`
`/data/data/` (`manager/src/io/github/rianprei/bepinex/manager/LogViewerActivity.java:35-66`).

Restaurar o switch ao estado original e usar **Reiniciar jogo**; não excluir
arquivos de terceiros.

### `.so`, `.bpatch` e `.bmod`

No Manager, usar **+ Instalar mod**, escolher um `.so` de teste e confirmar a
mensagem `Instalado:` (`manager/src/io/github/rianprei/bepinex/manager/core/LooseModInstaller.java:104`).
Escolher um `.bmod` compatível; confirmar a tela de confirmação e a mensagem
`Este pacote foi feito para o jogo` (`manager/src/io/github/rianprei/bepinex/manager/MainActivity.java:363`).

Para `.bpatch`, usar um pacote produzido por F4 e confirmar que o arquivo chega
em `/data/local/tmp/mods/$PKG/`.

```bash
adb shell su -c "ls -lZ /data/local/tmp/mods/$PKG"
```

Restaurar removendo apenas os arquivos criados nesta rodada via **Remover** ou
`SuHelper.deleteMod`; conservar o backup.

### Crashguard e Reativar

```bash
. tools/symbols.sh && symbols_ship mods/t_crash/libs/arm64-v8a/libt_crash.so /tmp/t_crash.so
adb push /tmp/t_crash.so /data/local/tmp/t_crash.so
adb shell su -c "cp /data/local/tmp/t_crash.so /data/local/tmp/mods/$PKG/t_crash.so"
adb shell am force-stop "$PKG"; adb shell monkey -p "$PKG" 1
sleep 4
adb shell monkey -p "$PKG" 1
sleep 4
adb shell su -c "grep 'mods desativados: o jogo fechou 2x logo depois de carregar — reative no Manager' /data/data/$PKG/files/bepinex/log.txt"
```

Esperado: `carregado: vou abortar em %ds` (`mods/t_crash/jni/t_crash_mod.cpp:30`)
e `abortando de proposito: teste do crashguard (F1d)` (`mods/t_crash/jni/t_crash_mod.cpp:25`),
seguido de `mods desativados: o jogo fechou 2x logo depois de carregar — reative no Manager` (`jni/main.cpp:1724-1732`).
Na terceira abertura o mod não deve carregar.
No Manager, o banner deve oferecer **Reativar**; a ação apaga marcador e
contador (`GameDetailActivity.java:137-159`, `SuHelper.java:389-405`).
Restaurar removendo `t_crash.so` e pressionando Reativar.

## F12 — spike CoreCLR

Este item é uma medição de pesquisa, não uma instalação de `.dll`: o roadmap
registra que o caminho Zygisk ainda não existe (`docs/ROADMAP-UNIVERSAL.md:F12`).
Não colocar CoreCLR, `Il2CppInterop` ou uma DLL de PC no jogo nesta rodada.
Registrar apenas os números fornecidos pelo estudo em
`raw/f12-spike.md`: tamanho do payload, RAM e tempo da primeira execução. O
go/no-go depende de uma implementação separada e de dados reais do device;
se uma futura build de teste existir, medir com:

```bash
adb shell dumpsys meminfo "$PKG" > /tmp/sa2-before-coreclr.txt
adb shell am force-stop "$PKG"; adb shell monkey -p "$PKG" 1
adb shell dumpsys meminfo "$PKG" > /tmp/sa2-after-coreclr.txt
```

Restaurar é simplesmente não instalar o artefato experimental; apagar somente
os dois arquivos locais de medição.

## F6 — Scanner e Mod Maker

No Manager, abrir **Escanear Jogo**. O APK deve ter `assets/u_dump.so`; instalar
o scanner, deixar o jogo iniciar e aguardar até 240s. A UI esperada é
`Scanner concluído: dump.tsv gerado.` ou `Erro ao escanear: ...`
`Scanner concluído: dump.tsv gerado.` (`manager/src/io/github/rianprei/bepinex/manager/ModMakerActivity.java:167-197`). O arquivo esperado é:

```bash
adb shell su -c "test -s /data/data/$PKG/files/bepinex/dump.tsv && grep -E 'ComplexCreature|HasAmmo' /data/data/$PKG/files/bepinex/dump.tsv"
adb shell su -c "test ! -e /data/local/tmp/mods/$PKG/u_dump.so"
```

O log do mod deve conter `dump.tsv pronto: %lld linhas, %zu assemblies` (`mods/u_dump/jni/u_dump_mod.cpp:297`).
Confirmar busca paginada por classe,
método e campo, e gerar uma regra pela UI. Restaurar apagando apenas o dump
de teste pelo botão/Manager; o scanner é removido sempre pelo fluxo.

## F9b — u_noads com sa2content desligado

```bash
adb shell su -c "mv /data/local/tmp/mods/$PKG/sa2content.so /data/local/tmp/mods/$PKG/sa2content.so.off"
adb shell am force-stop "$PKG"; adb shell monkey -p "$PKG" 1
adb shell su -c "grep -E 'u_noads|fechamento falhou|suprimido com fechamento' /data/data/$PKG/files/bepinex/log.txt"
```

Esperado: `intersticial/app-open suprimido com fechamento do próprio SDK` (`mods/u_noads/jni/u_noads_mod.cpp:70-76`);
se o callback não for seguro, `fechamento falhou%s; hook recusado, anúncio volta a aparecer` (`mods/u_noads/jni/u_noads_mod.cpp:70-76`).
Verificar
que anúncios forçados entre fases não aparecem e que rewarded/IAP continuam
normais. Depois religar:

```bash
adb shell su -c "mv /data/local/tmp/mods/$PKG/sa2content.so.off /data/local/tmp/mods/$PKG/sa2content.so"
adb shell am force-stop "$PKG"; adb shell monkey -p "$PKG" 1
adb shell su -c "grep -E 'sa2content|item\\(ns\\) trocados|Apply (ok|LANÇOU EXCEÇÃO)' /data/data/$PKG/files/bepinex/log.txt"
```

Esperado: `item(ns) trocados, Apply %s (retorno %d)` (`mods/sa2content/jni/sa2content_mod.cpp:87-90`).
Restaurar o estado original
do `.so.off`.

## F11 — Frida smoke e gadget renomeado

Com `tools/deploy_frida.sh "$PKG" smoke.js`, abrir o jogo e confirmar:

```bash
adb shell su -c "grep -E 'il2cpp ok, carregando gadget|gadget ativo|dlopen do gadget falhou|config fora do modo script' /data/data/$PKG/files/bepinex/log.txt"
```

Esperado: `gadget ativo (type=%s, pasta %s, 1º script %s)` (`mods/u_frida/jni/u_frida_mod.cpp:105-130`).
O smoke deve
escrever sua marca em `files/bepinex/`. Para a guarda, copiar o gadget real com
nome `renamed_mod.so` para a pasta de mods e abrir o jogo:

```bash
adb shell su -c "cp frida-gadget.bin /data/local/tmp/mods/$PKG/renamed_mod.so"
adb shell am force-stop "$PKG"; adb shell monkey -p "$PKG" 1
adb shell su -c "grep -E 'frida-gadget|renamed_mod' /data/data/$PKG/files/bepinex/log.txt"
```

Esperado: `parece o frida-gadget (soname %s): recusado` (`jni/main.cpp:1819-1822`),
com recusa antes do `dlopen`. Restaurar removendo
`renamed_mod.so`, `frida-gadget.bin`, config e `.js`.

## Soak e coleta final

Com o conjunto normal de mods, jogar SA2 por **10 minutos** (trocar arma,
recarregar, entrar/sair de fase, abrir a tela do Manager sem reiniciar o jogo):

```bash
adb logcat -c
adb shell monkey -p "$PKG" 1
sleep 600
adb logcat -d -b all > /tmp/sa2-round.log
adb logcat -d -b all | grep -E 'FATAL EXCEPTION|SIGSEGV|ANR|avc: denied'
adb shell su -c "cat /data/data/$PKG/files/bepinex/log.txt" > /tmp/sa2-log.txt
```

Esperado: nenhum crash/ANR; guardar qualquer `avc: denied` com `permissive=0`
e o contexto/arquivo. Comparar tamanho de `log.txt` no início/fim e anexar
`/tmp/sa2-round.log` e `/tmp/sa2-log.txt` ao relatório. Restaurar removendo
somente artefatos da rodada, reativando `sa2content.so` e voltando SELinux ao
valor original:

```bash
adb shell su -c "setenforce 0"   # somente se esse era o valor original
adb shell su -c "rm -rf /data/local/tmp/round-backup"
```

Não apagar o backup do save do SA2; devolvê-lo ao usuário separadamente após
confirmar que o jogo abre normal.
