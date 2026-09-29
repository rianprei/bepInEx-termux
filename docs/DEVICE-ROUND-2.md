# Rodada 2 no aparelho — POCO C75 / HyperOS / Android 16 / Magisk

Roteiro único com usuário presente; estimativa **40–55 minutos**, incluindo
10 minutos de jogo. A seção de `mods-reloc` é dependência futura e não entra
nessa estimativa nem pode ser marcada como aprovada nesta rodada.

## Limites e preflight

- Não editar, substituir ou ler APK/OBB/save. Os arquivos temporários abaixo
  são um `.bpatch` de teste em Download e artefatos de mod em
  `/data/local/tmp/mods/<pkg>/`.
- Anotar o valor original de `getenforce`, versão do Manager/módulo e os nomes
  de `mods/<pkg>` antes do primeiro teste. Não trocar SELinux nesta rodada,
  exceto no bloco explicitamente dependente de `mods-reloc`.
- `tools/device_test.sh` guarda e restaura `mods/<pkg>` e toda a pasta
  `files/bepinex`, confere os hashes e deixa o jogo parado. Não interromper o
  script; se interromper, ele restaura e confere antes de sair.
- O teste de Download exige que estes nomes ainda não existam:
  `/sdcard/Download/round2-picker-probe.bpatch` e
  `/data/local/tmp/mods/<pkg>/round2-picker-probe.bpatch`. Se qualquer um
  existir, pare e escolha outro nome antes de prosseguir; não sobrescreva.
- Pré-condição de `field`: SA2 aberto numa fase com arma equipada e contador
  de munição visível. O operador fará **um disparo** durante a pausa do kit.
- Instale antes um build que contenha a implementação corrigida de `field`;
  esta branch só prepara o roteiro/kit. Sem esse build, marque a etapa como
  bloqueada e não interprete o resultado como falha do aparelho.
- A etapa Frida requer `u_frida.so` já instalado em `mods/<pkg>`; o
  `deploy_frida.sh` prepara o gadget e o script, mas não instala essa `.so`.
  Se ela faltar, marque Frida como bloqueado, não como PASS.

`PKG=com.hyperdotstudios.swampattack2`. Build e conexão devem estar prontos;
esta rodada não instala release, atualiza módulo nem reinicia o telefone.

## Passos com resultado objetivo

| Etapa / tempo | Ação do usuário (uma ação por linha) | O que o kit confere | PASS / FAIL | Restore |
|---|---|---|---|---|
| 1. `field` — 3–5 min | Execute `tools/device_test.sh "$PKG" test/device/sa2-field 120 --hold-after-pass=90`. Quando aparecer `MANUAL: expectativas satisfeitas`, toque **uma vez** no botão de disparo do SA2. | O kit instala `t1_field.bpatch`, exige a linha de `field aplicado`, mantém o jogo aberto por 90 s, verifica PID/crash e restaura os snapshots de mods e `files/bepinex`. | **PASS** somente se o kit termina `RESULTADO: PASS`, o jogo continua vivo e o número de munição não diminui após o disparo. Sem a linha, crash ou munição decrementada = **FAIL**. O efeito visual é observado pelo usuário, não inferido do log. | Automático pelo kit, com comparação de SHA-256. O jogo fica parado após restore. |
| 2a. Download — preparo | No host, confira que os dois destinos não existem e só então execute `adb push test/device/download-picker/round2-picker-probe.bpatch /sdcard/Download/round2-picker-probe.bpatch`. | Nenhum toque no telefone. O fixture contém uma regra sintaticamente válida para uma classe inexistente; ela não deve alterar gameplay. | **PASS** se o push termina e os nomes de destino estavam ausentes antes do push. Se houver colisão, pare; não sobrescreva. | O fixture é removido no passo 2g. |
| 2b. Abrir o instalador | Na tela do SA2 no Manager, toque **+ INSTALAR MOD**. | O seletor `ACTION_OPEN_DOCUMENT` continua sendo a primeira opção. | **PASS** se o seletor de arquivos abre; senão **FAIL**. | Nenhum arquivo instalado ainda. |
| 2c. Exercitar cancelamento sem URI | Volte uma vez do seletor sem escolher arquivo. | O Manager deve oferecer a frase “O seletor de arquivos deste celular não devolveu o arquivo. Quer escolher direto da pasta Download?”. | **PASS** se o diálogo exato aparece após cancelamento sem URI; senão **FAIL**. | Nenhum arquivo instalado ainda. |
| 2d. Usar a lista root | Toque **Escolher da pasta Download**. | A lista nativa contém `round2-picker-probe.bpatch`; Download/Documents são listados pelo helper numa chamada root, somente arquivos regulares diretos, sem symlinks. | **PASS** se o nome aparece; ausente ou erro da lista = **FAIL**. | Nenhum arquivo instalado ainda. |
| 2e. Selecionar o arquivo | Toque uma vez em `round2-picker-probe.bpatch`. | O Manager deve exibir a confirmação/mensagem `Instalado: round2-picker-probe.bpatch`. | **PASS** se a instalação termina; erro, jogo pedido de novo ou `.so` tratado como zip = **FAIL**. | Remova somente o fixture em mods e Download (passo 2g). |
| 2f. Conferir bytes instalados | No host, execute `tools/device_round2_picker_check.sh "$PKG"`. | Uma chamada root compara o arquivo regular de Download e o destino byte a byte; symlink ou diferença falha. | **PASS** só com `PASS: arquivo selecionado em Download foi instalado byte a byte`; caso contrário **FAIL**. | O verificador é somente leitura. |
| 2g. Limpar o fixture | No host, execute `adb shell rm -f /sdcard/Download/round2-picker-probe.bpatch` e `printf 'rm -f /data/local/tmp/mods/%s/round2-picker-probe.bpatch\n' "$PKG" \| adb shell su`. | Confira que ambos agora estão ausentes. Se o Manager instalou `u_patch.so` e ela não existia no inventário inicial, remova apenas essa cópia nova; não apague uma que já existia. | **PASS** se só os caminhos de teste foram removidos e o inventário restante bate com o anterior. | Logs de operação ficam como evidência; não apague `files/bepinex` inteiro. |
| 3. Logs de boot IL2CPP — 3–5 min | Execute `tools/device_test.sh "$PKG" test/device/il2cpp-wait 120`. | Fixture de `expect.txt` exige uma rota `__loader_dlopen` ou fallback `dlopen` e `runtime pronto após Ns`; o kit também verifica crash e restaura arquivos. Requer `uni/il2cpp-wait-2` integrada no módulo instalado. | **PASS** se as duas linhas aparecem e o kit termina PASS. Ausência de rota/boot, crash ou versão ainda antiga = **FAIL / dependência não pronta**, não prova contra o aparelho. | Automático pelo kit. |
| 4a. Preparar Frida 17.18.0 — 2–3 min | Se `frida-gadget.bin` ou o script ainda não estiverem instalados, execute `tools/deploy_frida.sh "$PKG" test/device/sa2/frida_smoke.js`. | Nenhum toque no telefone. O hash do gadget será verificado no passo 4c. | **PASS** se o instalador termina sem erro; qualquer falha = **FAIL**. | Antes de sobrescrever arquivos existentes, faça cópias individuais e restaure-as ao final; preserve `u_frida.so`. |
| 4b. Executar smoke Frida — 2–3 min | No host, execute `tools/device_test.sh "$PKG" test/device/sa2 120`. | O kit exige expectativas do `u_patch` e `[u_frida] gadget ativo` no log, verifica `frida_ok.txt` e `frida_count.txt`, detecta crash e restaura mods/`files/bepinex`. Se faltar gadget, o `SKIP` não satisfaz a expectativa de log. | **PASS** se termina `RESULTADO: PASS`, sem SKIP, com ambos os arquivos e sem crash. Isso verifica este script no SA2, não todos os scripts Frida. | Restore automático do kit; depois restaure somente os arquivos Frida que já existiam antes do passo 4a. |
| 4c. Conferir pin — 1 min | Execute `tools/device_round2_frida_check.sh "$PKG"`. | O helper compara o SHA-256 do `frida-gadget.bin` instalado com o pin 17.18.0. | **PASS** só com hash igual ao pin; qualquer outro resultado = **FAIL**. | Somente leitura. |
| 5a. Ativar processo `:sufixo` — 1–2 min, condicional | Se algum jogo instalado tiver processo secundário normal, abra uma vez o recurso que o inicia. | O operador identifica o jogo/recurso; não há jogo com esse processo = **SKIP** da etapa. | Prosseguir para 5b somente se o processo existir. | Nenhum arquivo é alterado. |
| 5b. Conferir processo `:sufixo` — 1 min | No host, execute `tools/device_round2_process_check.sh <pkg> <mod.so>`. | A ferramenta busca `<pkg>:<sufixo>` e verifica no `/proc/<pid>/maps` se o `.so` escolhido está mapeado. | **PASS** se o mod aparece em ao menos um processo secundário. Ausência do processo = **SKIP** (`exit 3`, inconclusivo); processo encontrado sem o `.so` = **FAIL**. | Nenhum arquivo é alterado. Feche o jogo quando terminar. |
| 6. Soak — 10 min | Execute `tools/device_round2_soak.sh "$PKG"` e mantenha o SA2 em primeiro plano jogando durante os 600 s. | O helper salva logcat anterior no host, inicia o jogo, espera 600 s e guarda logcat, log do mod, PID e SELinux em `out/device-round2/`. Confere processo original vivo, crash/ANR e AVCs do pacote/caminhos de mods. | **PASS** se o helper termina `RESULTADO: PASS` e o operador confirma que jogou em primeiro plano durante toda a espera; crash, ANR, PID original morto ou AVC relacionado ao pacote/mod = **FAIL**. AVC de outro componente é preservado para análise e não reprova sozinho. | Não muda arquivos do jogo, mods nem SELinux. O jogo fica aberto; restaure manualmente o estado de tela desejado. O buffer de logcat é limpo, mas o anterior fica salvo na pasta de evidências. |

O helper de soak coleta evidências; não automatiza a ação de jogar nem prova
que um efeito visual ocorreu. O efeito de `field` e os controles de tela só
podem ser observados pelo usuário.

### 7. `uni/mods-reloc` — árvore root-only e entrega por FD (Enforcing)

> **Não executar antes do merge da `uni/mods-reloc`.** Este passo substitui a
> seção "Dependência bloqueada" que estava aqui. O `device_test.sh` desta
> branch já snapshota a árvore NOVA (`/data/adb/bepinex/mods/<pkg>` — a
> migração entrou no kit), mas o passo 7 cobre o que o kit não cobre: leitura
> e uma escrita controlada na árvore root-only em Enforcing.

O que este passo prova, e o que ele **não** prova:

| # | Ação | O que prova | PASS | FAIL |
|---|---|---|---|---|
| 7a | `adb shell getenforce` | O aparelho está em Enforcing. | `Enforcing`. | `Permissive` = **FAIL**, e nenhum outro passo vale. Enforcing é o que torna a política SELinux uma barreira de verdade — a maioria dos aparelhos de teste roda em Permissive, e foi por isso que o `dlopen` por caminho "funcionava". | 
| 7b | `adb shell ls /data/adb/bepinex` | O **shell** (uid 2000) não alcança a árvore. | não lista nada. | Se o shell ler, a raiz não está root-only e o resto é teatro. |
| 7c | `adb shell su -c "ls -laZ /data/adb/bepinex/mods/<pkg>"` | A árvore existe, é do root, e tem o rótulo novo. | `root root` + `u:object_r:bepinex_mod_file:s0`. | Dono/grupo diferente, ou contexto ausente. |
| 7d | `adb shell su -c "mv /data/adb/bepinex/mods /data/adb/bepinex/mods.x; ln -s /sdcard /data/adb/bepinex/mods; ls -la /data/adb/bepinex"` | O root **não** segue o link que o shell plantou. | `bc_mods` (ou o link) recusado/ausente, e `/data/adb/bepinex-migrate.log` com a linha `e link simbolico — NAO migrado`. | O link entra em vigor: a escrita do root passou a ser controlada pelo shell, que é exatamente o ataque que a mudança fecha. **Limpe** com `su -c "rm -f /data/adb/bepinex/mods; mv /data/adb/bepinex/mods.x /data/adb/bepinex/mods"`. |
| 7e | Instale um mod pelo Manager, abra o jogo, espere 30 s | O jogo carrega o mod **sem** abrir caminho, e o log foi para o lugar novo. | `/data/data/<pkg>/files/bepinex/log.txt` mostra o mod carregado. | Mod não carrega com o companion parado:companion mudo tem que virar "mod não carrega" + log, **não** jogo travado. Confira com `adb logcat` que não há trava. |
| 7f | Verifique o local do log | O log foi para o state dir do próprio app, não para `/data/local/tmp`. | `su -c "ls /data/data/<pkg>/files/bepinex/"` mostra `bc_poc_LogOutput.log`; `ls /data/local/tmp/bc_poc_LogOutput.log` **não** existe. | Log no lugar antigo = a escrita do jogo ainda depende do diretório 0777 que a mudança removeu. |
| 7g | `adb shell su -c "cat /proc/<pid>/maps" \| grep bepinex` | O `.so` do mod está mapeado, e o caminho mapeado é o **novo**. | a linha do `.so` aponta para `/data/adb/bepinex/...`. | Mapeado de `/data/local/tmp/...` = o jogo ainda abriu o caminho velho. |
| 7h | Mate o jogo duas vezes em 20 s | O crashguard escreve na árvore nova e a próxima partida não carrega mod. | `/data/adb/bepinex/mods/<pkg>/disabled_by_crashguard` existe, e o log do jogo diz que o mod foi barrado. | Sem marcador: o crashguard não acompanha a árvore nova. |
| 7i | `adb shell su -c "rm -rf /data/adb/bepinex/mods/<pkg>/disabled_by_crashguard"`, abra o jogo | O mod volta. | Carrega de novo. | Não volta: estado preso. |
| 7j | `adb logcat -d \| grep "avc: denied"` | Nenhum AVC do pacote neste caminho. | Nenhum `avc: denied` com `permissive=0` para `<pkg>` ou para `/data/adb/bepinex`. | Qualquer denial = **FAIL**; a sepolicy está incompleta. |

**SKIP, nunca PASS**, em dois casos: o aparelho não consegue ficar em
`Enforcing` (passo 7a), ou o `nmagisk`/instalação não_apply com sucesso.

**7k. Canal de pedidos do jogo no @bc_companion (SELinux, item novo).** O
jogo agora CONECTA ao `@bc_companion` pós-specialize como `untrusted_app`
(canal REQ: lista/FD dos mods e confs, ver `jni/bc_req_channel.h`). Não há
regra `connectto`/`unix_stream_socket` no `module/sepolicy.rule` DE PROPÓSITO:
o Termux já conecta ao mesmo socket há rodadas de device inteiras
(`termux-console/bepinex-console` é o caminho do console) e Termux é o MESMO
domínio SELinux (`untrusted_app`) de qualquer jogo da Play Store — a permissão
vem da policy base do Magisk para o socket do daemon, não do módulo;
acrescentar regra com label chutado seria teatro com risco de label errada.
O que coletar se algo negar: `adb logcat -d | grep "avc: denied"` filtrando
`untrusted_app` + `unix_stream_socket`/`connectto` no launch do jogo com
mods — o rótulo REAL do socket do daemon aparece no próprio AVC, e é com
esse rótulo que a regra certa se escreve, nunca com palpite. O 7e cobre o
resto do canal: se os mods carregam pelo FD, o connectto passou.

**Ordem de reparo quando algo falha**, porque pular etapa esconde a causa:
se 7b/7c falham, a árvore não foi criada pelo `post-fs-data.sh` (veja
`/data/adb/bepinex-migrate.log`); se 7c passa e 7e falha, o problema é o
transporte (FD ou timeout), não a árvore; se 7d falha, o `O_NOFOLLOW` sumiu do
`companion.cpp`.

