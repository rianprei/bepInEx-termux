# jni/repro.mk — build reproduzível: os MESMOS bytes em QUALQUER máquina.
#
# ACHADO (device POCO C75, SA2, tombstone_07_f4field): o tombstone traz o
# build-id do u_patch.so (041d9b51...) e nada mais, porque o .so de release
# sai STRIPPED — sem .symtab e sem DWARF. Sem nome de função, um crash do
# usuário só produz um offset e um hash que não casa com nada.
#
# Pior: o build-id não é comparável nem entre builds do mesmo commit. O linker
# do NDK calcula o NT_GNU_BUILD_ID sobre os .o de ENTRADA, e o caminho do
# diretório de build está gravado no DWARF deles. Verificado nesta árvore:
#
#   mesmo commit (5ed8019), diretórios diferentes:
#     /tmp/.../repro-a -> 62c539ed35f464a0cc4ceb8cf35db0b6c213fc56
#     /tmp/.../repro-b -> e38e4fa87d67dc98669d9aaf0f61bcf0e4e0bde1
#
# Dois builds de release do MESMO commit, em máquinas diferentes, produzem
# dois build-id diferentes — e o cruzamento "qual .so era esse?" vira adivinhação.
# Foi exatamente o que aconteceu: para achar a linha do crash eu tive que
# compilar 12 commits e comparar layout na mão.
#
# O conserto é -ffile-prefix-map: reescreve o caminho do build para um prefixo
# fixo, então o DWARF (e o hash que sai dele) deixa de depender de ONDE o
# código foi compilado. Com isto, o build-id volta a ser um identificador
# confiável, e o tools/symbolize.sh consegue cruzar tombstone ↔ símbolos.
#
# Uso: este arquivo é incluso pelo Application.mk de CADA projeto (loader em
# jni/, cada mod em mods/<pkg>/jni/). Defina BEPINEX_REPRO_ROOT antes do
# include, apontando para a raiz do repo, que é onde vivem os headers
# compartilhados (mods/common, jni/) e cujos caminhos também entram no DWARF.

ifeq ($(BEPINEX_REPRO_ROOT),)
    $(error BEPINEX_REPRO_ROOT nao definido: o Application.mk deste projeto tem que setar antes de incluir jni/repro.mk)
endif

# O caminho tem que EXISTIR. Sem este teste o make emite "Sem regra para
# processar o alvo /caminho/jni/repro.mk" e a falha aparece como exit 2 no meio
# de um ndk-build de 20 alvos, sem dizer qual include está errado.
BEPINEX_REPRO_MK := $(BEPINEX_REPRO_ROOT)/jni/repro.mk
ifeq ($(wildcard $(BEPINEX_REPRO_MK)),)
    $(error jni/repro.mk nao encontrado em '$(BEPINEX_REPRO_MK)' — BEPINEX_REPRO_ROOT=$(BEPINEX_REPRO_ROOT). Loader: $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..); mod: $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..))
endif

BEPINEX_REPRO_PREFIX ?= /bepinex-termux

# --- a raiz do NDK, do NDK ----------------------------------------------------
#
# ACHADO DA REVISÃO DE a1783f5: a raiz do NDK era descoberta por um glob em
# "$HOME/Android/Sdk/ndk/*". Funciona NESTA máquina e só nesta: com o NDK em
# /opt/android-ndk, em ANDROID_NDK_HOME, num CI, ou no home de outro usuário, o
# glob não acha e o prefix-map da raiz do NDK SOME — que é justamente o item 3
# (caminho de máquina vaza e o build-id muda).
#
# $(NDK_ROOT) é a variável que o próprio NDK define em build/core/init.mk, e o
# init.mk entra em build-local.mk na LINHA 48, antes do add-application.mk (linha
# 199) que é quem inclui o Android.mk deste projeto. Ou seja: está definido
# exatamente no ponto de uso.
#
# $(NDK) NÃO serve: essa é a variável de AMBIENTE do ndk-build e ela não chega
# no make durante o parse. Foi o que deixou a busca anterior caindo no glob.
ifeq ($(strip $(NDK_ROOT)),)
    $(error NDK_ROOT vazio no make: o build nao esta rodando pelo ndk-build deste NDK (init.mk define e aborta se faltar), ou o Android.mk esta sendo included fora do NDK)
endif

# Só a raiz do NDK. Não $(CURDIR): o mesmo projeto é compilável de mais de um
# lugar (o verify_all e o build_module.sh chamam da raiz do repo; a pessoa na
# mão pode cd jni). Mapear o CURDIR colocaria o prefixo em /bepinex-termux ou
# /bepinex-termux/src conforme o cwd, e o build-id voltaria a depender do
# diretório — que é o que este arquivo existe para impedir.
#
# -ffile-prefix-map cobre __FILE__ e o DWARF; -fdebug-prefix-map é o mesmo
# para o DWARF (o clang aceita os dois, e manter os dois deixa a intenção
# explícita para quem for mexer nisso depois).
BEPINEX_REPRO_FLAGS := \
    -ffile-prefix-map=$(BEPINEX_REPRO_ROOT)=$(BEPINEX_REPRO_PREFIX) \
    -fdebug-prefix-map=$(BEPINEX_REPRO_ROOT)=$(BEPINEX_REPRO_PREFIX) \
    -ffile-prefix-map=$(NDK_ROOT)=$(BEPINEX_REPRO_PREFIX)/ndk \
    -fdebug-prefix-map=$(NDK_ROOT)=$(BEPINEX_REPRO_PREFIX)/ndk

APP_CFLAGS += $(BEPINEX_REPRO_FLAGS)
APP_CPPFLAGS += $(BEPINEX_REPRO_FLAGS)

# Símbolos sobrevivem ao link. O ndk-build stripa por padrão no passo Install,
# e é esse passo que matava o .symtab e o DWARF do .so de release.
#
# Não aumentar o que vai pro device: o tools/build_release.sh faz o strip
# EXPLICITO do binário de release, e o não-stripado vai para symbols/.
# Aqui só garantimos que a informação exista antes de ser dividida em dois.
APP_STRIP_MODE := none

# --- prebuilt de terceiro sem caminho de quem o compilou ---------------------
#
# ACHADO DA REVISÃO DE f59e9ff (BAIXA): o arquivo de símbolos de uma release
# carregava /home/rianprei/... e /home/rianprei/battlecats-mods/Dobby/...,
# vindos do DWARF do prebuilt jni/lib/arm64-v8a/libdobby.a. A release é
# pública: um release distribuído não pode levar o diretório home e o nome de um
# projeto pessoal de quem montou.
#
# A correção é REMOVER o DWARF do prebuilt no build, com objcopy, e linkar
# contra a cópia limpa. O que o prebuilt de fato nos dá são os SÍMBOLOS (Dobby
# é uma biblioteca de hook: o backtrace precisa de nome de função).
#
# O que isso NÃO muda: o .text. Conferido membro a membro nos 40 objetos do
# libdobby.a — .text byte a byte igual antes e depois. E os símbolos definidos
# são idênticos (llvm-nm --defined-only, diff vazio).
#
# O que muda: perdemos o NÚMERO DE LINHA das funções internas do Dobby. Um crash
# dentro da trampoline ainda resolve para o nome da função, que é o que o
# symbolize.sh precisa. O prebuilt VERSIONADO não é alterado — a cópia limpa é
# do build.
#
# Uso no Android.mk:
#   LOCAL_SRC_FILES := $(call bepinex_prebuilt,jni/lib/$(TARGET_ARCH_ABI)/libdobby.a)
#
# O caminho é SEMPRE relativo à RAIZ do repo (que este arquivo já conhece), e
# não ao jni/ do módulo: o prebuilt mora na raiz em qualquer caso, e usar
# LOCAL_PATH aqui dependia de ele já estar resolvido no momento em que a
# variável é expandida — e no loader e nos mods ele não é.

# O objcopy DO NDK DESTE BUILD. O glob é dentro de $(NDK_ROOT) (e não em
# $HOME), porque o host tag varia (linux-x86_64, darwin-x86_64, darwin-arm64) e
# o NDK é a única fonte da verdade. $(NDK_ROOT) não tem como faltar: o init.mk
# aborta o build se faltar.
BEPINEX_OBJCOPY := $(firstword $(wildcard \
    $(NDK_ROOT)/toolchains/llvm/prebuilt/*/bin/llvm-objcopy))
ifeq ($(strip $(BEPINEX_OBJCOPY)),)
    $(error llvm-objcopy nao encontrado em '$(NDK_ROOT)/toolchains/llvm/prebuilt/*/bin/llvm-objcopy'. Sem ele o prebuilt volta a levar o caminho de quem o compilou. Defina BEPINEX_OBJCOPY=... para apontar um binario.)
endif

# b.epinex_prebuilt <caminho-relativo-a-raiz>
#
# Devolve o caminho da cópia limpa, ou ABORTA com a mensagem.
#
# A antes-disto devolvia "" em qualquer erro, e o ndk-build quebrava depois com
# "LOCAL_SRC_FILES should only contain one item" — uma mensagem que não diz
# NADA sobre a causa. Pior: se o link por acaso funcionasse (o objcopy falha
# mas o arquivo antigo continua lá), o prebuilt inteiro voltava a vazar sem
# ninguém ver. Falha de limpeza tem que ser BARULHENTA e nomeada.
#
# O shell nunca devolve vazio: em erro ele imprime "BEPINEX-ERRO <motivo>", e a
# verificação acontece numa SEGUNDA call. Isso é proposital: num único corpo de
# define, o make expande tudo de uma vez e um $(eval) no meio chega tarde demais
# para o $(if) que vem depois (verificado: dava string vazia com _v já
# preenchido depois). Passando o valor como ARGUMENTO de outra call, o shell
# roda primeiro e o $(error) enxerga o resultado.
# A chave do cache da cópia limpa é o sha256 do CONTEÚDO do prebuilt E dos
# argumentos do objcopy, E da versão do objcopy.
#
# ACHADO DA REVISÃO DE b384771: com a chave sendo só o conteúdo, trocar
# --strip-debug por uma remoção parcial de seções NÃO gerava cópia nova — o
# ship_stripped_test reencontrava a cópia limpa antiga do cache, dava OK, e o
# vazamento voltava sem ninguém ver. A chave tem que mudar quando o que a
# FERRAMENTA FAZ muda, não só quando a entrada muda.
#
# A chave é montada com um printf de uma string só (e não com um grupo
# `{ ...; }` multi-linha), e NÃO há comentário dentro do $(shell): um `#` em
# make encerra a linha lógica e deixa o comando pela metade — foi o que
# quebrou a primeira tentativa.
define bepinex_prebuilt
$(call _bp_check,$(strip $(shell \
    src="$(BEPINEX_REPRO_ROOT)/$(1)"; \
    if [ ! -f "$$src" ]; then echo "BEPINEX-ERRO prebuilt ausente: $$src"; exit 0; fi; \
    if ! command -v sha256sum >/dev/null 2>&1; then \
        echo "BEPINEX-ERRO sha256sum ausente: o nome da copia limpa e o sha256 do conteudo"; \
        exit 0; \
    fi; \
    outdir="$(BEPINEX_REPRO_ROOT)/obj/prebuilt-limpo"; \
    if ! mkdir -p "$$outdir"; then echo "BEPINEX-ERRO nao criei $$outdir"; exit 0; fi; \
    BEPINEX_PB_FLAGS='--strip-debug'; \
    src_sum=$$(sha256sum "$$src" | cut -d' ' -f1); \
    oc_ver=$$($(BEPINEX_OBJCOPY) --version 2>/dev/null | head -1); \
    [ -n "$$oc_ver" ] || oc_ver='sem-versao'; \
    sum=$$(printf '%s|%s|%s' "$$src_sum" "$$BEPINEX_PB_FLAGS" "$$oc_ver" \
            | sha256sum | cut -d' ' -f1); \
    out="$$outdir/$$sum.a"; \
    if [ ! -f "$$out" ]; then \
        tmp=$$(mktemp "$$outdir/.tmp.XXXXXX") || { \
            echo "BEPINEX-ERRO mktemp falhou em $$outdir"; exit 0; }; \
        if ! $(BEPINEX_OBJCOPY) $$BEPINEX_PB_FLAGS "$$src" "$$tmp" 2>/dev/null; then \
            rm -f "$$tmp"; \
            echo "BEPINEX-ERRO objcopy $$BEPINEX_PB_FLAGS falhou em $$src (objcopy: $(BEPINEX_OBJCOPY))"; \
            exit 0; \
        fi; \
        mv -f "$$tmp" "$$out" || { rm -f "$$tmp"; \
            echo "BEPINEX-ERRO mv falhou em $$out"; exit 0; }; \
    fi; \
    echo "$$out")))
endef

define _bp_check
$(if $(filter BEPINEX-ERRO,$(firstword $(1))),\
    $(error bepinex_prebuilt: $(wordlist 2,$(words $(1)),$(1))),\
    $(1))
endef

# --- gancho de prova (test/symbols/ndk_path_test.sh) ------------------------
#
# Fica NO FIM de propósito: dentro de um $(warning), o make expande na hora, e
# as variáveis ainda não existiam no meio do arquivo (OBJCOPY vinha vazio).
#
# "NDK_ROOT está definido no ponto de uso" só se prova DENTRO de um ndk-build de
# verdade: em make puro, sem o init.mk do NDK, NDK_ROOT estaria vazio e a
# afirmação seria falsa — foi o que a primeira versão deste check mediu.
#
# O NDK resolve o próprio caminho (um symlink vira o caminho real), então o que
# entra no prefix-map é o caminho REAL que o compilador vai ver. O gate compara
# contra o NDK_ROOT observado, e não contra o symlink.
ifeq ($(BEPINEX_REPRO_DEBUG),1)
$(warning BEPINEX-PROBE NDK_ROOT=[$(NDK_ROOT)] OBJCOPY=[$(BEPINEX_OBJCOPY)] FLAGS=[$(BEPINEX_REPRO_FLAGS)])
endif
