ifeq ($(BEPINEX_OBJCOPY),)
# $(shell) roda em sh, nao bash: por isso o for com "c in ...", e nada de
# $(...) aninhado. O globs do NDK cobrem qualquer versao instalada.
BEPINEX_OBJCOPY := $(shell \
    if [ -n "$$NDK" ] && [ -x "$$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objcopy" ]; then \
        printf '%s' "$$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objcopy"; \
    else \
        for c in "$$HOME"/Android/Sdk/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objcopy; do \
            if [ -x "$$c" ]; then printf '%s' "$$c"; break; fi; \
        done; \
    fi)
endif# jni/repro.mk — build reproduzível: os MESMOS bytes em QUALQUER diretório.
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
# Dois builds de release do MESMO commit, em máquinas diferentes, produce
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

# O objcopy que tira o DWARF do prebuilt (ver bepinex_prebuilt, no fim).
#
# Busca o do toolchain e cai para o do PATH. $(NDK) NAO chega no make durante o
# parse do Android.mk — era isso que deixava a variavel vazia e a limpeza
# falhar com "objcopy falhou", e o prebuilt inteiro voltava a vazar o /home de
# quem o compilou. Por isso a lista de candidatos e explicita.
# Define BEPINEX_OBJCOPY=... para forcar um binario.
ifeq ($(BEPINEX_OBJCOPY),)
BEPINEX_OBJCOPY := $(shell \
    for c in "$(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objcopy" \
             "$(HOME)"/Android/Sdk/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objcopy \
             "$$(command -v llvm-objcopy 2>/dev/null)" \
             "$$(command -v objcopy 2>/dev/null)"; do \
        if [ -x "$$c" ]; then printf '%s' "$$c"; break; fi; \
    done)
endif

# Só a RAIZ é mapeada, nunca $(CURDIR).
#
# $(CURDIR) é o diretório de onde o ndk-build foi CHAMADO, e o mesmo projeto é
# compilável de mais de um lugar (o verify_all chama o loader da raiz do repo,
# o build_module.sh também, e a pessoa na mão pode cd jni). Mapear o CURDIR
# colocaria o prefixo do resultado em /bepinex-termux ou /bepinex-termux/src
# conforme o cwd, e o build-id voltaria a depender do diretório — que é
# exatamente o que este arquivo existe para impedir. Mapeando só a raiz, o
# mesmo commit dá o mesmo build-id de qualquer diretório, inclusive de dentro
# de jni/.
#
# -ffile-prefix-map cobre __FILE__ e o DWARF; -fdebug-prefix-map é o mesmo
# para o DWARF (o clang aceita os dois, e manter os dois deixa a intenção
# explícita para quem for mexer nisso depois).
# A RAIZ DO NDK TAMBEM. Sem ela, o DWARF carrega
# /home/<voce>/Android/Sdk/ndk/<versao>/sysroot/usr/include... — que e a mesma
# clase de vazamento do prebuilt (o home de quem compilou) e, pior, quebra a
# reprodutibilidade: o mesmo commit, numa maquina com o NDK em outro caminho,
# daria um .so e um build-id diferentes. O prefixo /ndk e fixo.
# A raiz do NDK tambem. Sem ela, o DWARF carrega
# /home/<voce>/Android/Sdk/ndk/<versao>/sysroot/usr/include... — a mesma classe de
# vazaamento do prebuilt (o home de quem compilou) e, pior, quebra a
# reprodutibilidade: o mesmo commit, numa maquina com o NDK em outro caminho,
# daria um .so e um build-id diferentes. O prefixo /ndk e fixo.
#
# $NDK NAO chega no make neste ponto (so no shell, mais tarde), entao a raiz vem
# do mesmo glob que o objcopy usa. Mais de um NDK instalado nao faz mal: cada um
# que existir ganha a sua propria replace, e o que nao for usado e inerte.
BEPINEX_REPRO_FLAGS_NDK := $(shell \
    for d in "$$HOME"/Android/Sdk/ndk/*; do \
        [ -d "$$d" ] || continue; \
        printf ' -ffile-prefix-map=%s=%s/ndk -fdebug-prefix-map=%s=%s/ndk' \
            "$$d" "$(BEPINEX_REPRO_PREFIX)" "$$d" "$(BEPINEX_REPRO_PREFIX)"; \
    done)

BEPINEX_REPRO_FLAGS := \
    -ffile-prefix-map=$(BEPINEX_REPRO_ROOT)=$(BEPINEX_REPRO_PREFIX) \
    -fdebug-prefix-map=$(BEPINEX_REPRO_ROOT)=$(BEPINEX_REPRO_PREFIX) \
    $(BEPINEX_REPRO_FLAGS_NDK)

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
# é uma biblioteca de hook: o backtrace precisa de nome de função), e o
# llvm-objcopy tira só as seções .debug_*, que não entram em código nem em
# símbolo.
#
# O que isso NÃO muda: o .text. Conferido membro a membro nos 40 objetos do
# libdobby.a — .text byte a byte igual antes e depois. E os símbolos definidos
# são idênticos (llvm-nm --defined-only, diff vazio).
#
# O que muda: perdemos o NÚMERO DE LINHA das funções internas do Dobby. Um crash
# dentro da trampoline ainda resolve para o nome da função, que é o que o
# symbolize.sh precisa. Em troca, o release não carrega o caminho de ninguém.
# O prebuilt versionado NÃO é alterado — a cópia limpa é do build.
#
# Uso no Android.mk:
#   LOCAL_SRC_FILES := $(call bepinex_prebuilt,jni/lib/$(TARGET_ARCH_ABI)/libdobby.a)
#
# O caminho é SEMPRE relativo à RAIZ do repo (que este arquivo já conhece), e
# não ao jni/ do módulo: o prebuilt mora na raiz em qualquer caso, e usar
# LOCAL_PATH aqui dependia de ele ja estar resolvido no momento em que a
# variável é expandida — e no loader e nos mods ele não é.
define bepinex_prebuilt
$(strip $(shell \
    src="$(BEPINEX_REPRO_ROOT)/$(1)"; \
    [ -f "$$src" ] || { echo "bepinex: prebuilt ausente: $$src" >&2; echo ""; exit 0; }; \
    outdir="$(BEPINEX_REPRO_ROOT)/obj/prebuilt-limpo"; \
    mkdir -p "$$outdir" || exit 0; \
    out="$$outdir/$(notdir $(1))"; \
    if [ ! -f "$$out" ] || [ "$$src" -nt "$$out" ]; then \
        $(BEPINEX_OBJCOPY) --remove-section=.debug_info \
            --remove-section=.debug_abbrev --remove-section=.debug_line \
            --remove-section=.debug_str --remove-section=.debug_loc \
            --remove-section=.debug_ranges --remove-section=.debug_aranges \
            --remove-section=.debug_line_str --remove-section=.debug_str_offsets \
            "$$src" "$$out" 2>/dev/null || { echo "bepinex: objcopy falhou em $$src" >&2; echo ""; exit 0; }; \
    fi; \
    echo "$$out"))
endef
