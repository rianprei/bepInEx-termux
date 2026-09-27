# jni/repro.mk — build reproduzível: os MESMOS bytes em QUALQUER diretório.
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
BEPINEX_REPRO_FLAGS := \
    -ffile-prefix-map=$(BEPINEX_REPRO_ROOT)=$(BEPINEX_REPRO_PREFIX) \
    -fdebug-prefix-map=$(BEPINEX_REPRO_ROOT)=$(BEPINEX_REPRO_PREFIX)

APP_CFLAGS += $(BEPINEX_REPRO_FLAGS)
APP_CPPFLAGS += $(BEPINEX_REPRO_FLAGS)

# Símbolos sobrevivem ao link. O ndk-build stripa por padrão no passo Install,
# e é esse passo que matava o .symtab e o DWARF do .so de release.
#
# Não aumentar o que vai pro device: o tools/build_release.sh faz o strip
# EXPLICITO do binário de release, e o não-stripado vai para symbols/.
# Aqui só garantimos que a informação exista antes de ser dividida em dois.
APP_STRIP_MODE := none
