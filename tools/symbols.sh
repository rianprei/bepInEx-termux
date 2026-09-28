#!/usr/bin/env bash
# tools/symbols.sh — biblioteca de símbolos por build-id.
#
# POR QUE ISTO EXISTE. Um tombstone de crash do Android traz o build-id do .so
# e os offsets, e nada mais — porque o .so de release vai STRIPPED. O usuário
# manda o tombstone, e sem o binário não-stripado correspondente aquele offset
# é um número morto.
#
# E o build-id, sozinho, não ajuda: o linker do NDK calcula o NT_GNU_BUILD_ID
# sobre os .o de entrada, e o caminho do diretório de build está gravado no
# DWARF deles. Dois builds do MESMO commit em diretórios diferentes saíam com
# build-id diferente. Foi o que aconteceu no crash do SA2: o tombstone trazia
# 041d9b51..., e nenhum build local casava, então a linha do crash só saiu
# depois de compilar 12 commits e comparar layout na mão.
#
# O conserto tem duas partes, e as duas precisam estar:
#   1. jni/repro.mk (-ffile-prefix-map) faz o build-id depender do código, não
#      do diretório — depois disso ele identifica a build;
#   2. este script GUARDA o .so não-stripado de cada binário, indexado pelo
#      build-id, para o tools/symbolize.sh cruzar tombstone ↔ binário.
#
# Onde os símbolos ficam: $RELEASE_DIR/symbols/<build-id>/<nome>.so, ao lado da
# release, indexado por um INDEX. NUNCA dentro do zip Magisk nem do APK — o
# que vai pro device é o .so STRIPPED, e o tamanho importa.
set -euo pipefail

# --- toolchain ---------------------------------------------------------------
symbols_ndk() {
    local ndk="${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}"
    printf '%s' "$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
}

# Binário de strip do próprio NDK: mesma versão que gerou o .so, então o strip
# não introduz risco de corrói seções. Fallback para o llvm-strip do PATH.
symbols_strip_bin() {
    local p
    p="$(symbols_ndk)/llvm-strip"
    if [ -x "$p" ]; then printf '%s' "$p"; return 0; fi
    p="$(symbols_ndk)/llvm-strip.exe"
    if [ -x "$p" ]; then printf '%s' "$p"; return 0; fi
    command -v llvm-strip 2>/dev/null || command -v strip 2>/dev/null || true
}

# addr2line/symbolizer do NDK: precisam falar DWARM 5 do clang, e o binutils do
# host às vezes não lê.
symbols_addr2line() {
    local p
    p="$(symbols_ndk)/llvm-addr2line"
    [ -x "$p" ] && { printf '%s' "$p"; return 0; }
    command -v llvm-addr2line 2>/dev/null || command -v addr2line 2>/dev/null || true
}

# O readelf do NDK, ou o do PATH como reserva. `if` de verdade em vez de
# `A && B || C`: se o NDK existir mas a execução falhar, o `||` cairia no
# readelf do host em silêncio, e um binário de outro formato passaria por
# "sem build-id" em vez de "readelf falhou".
symbols_readelf() {
    local p
    p="$(symbols_ndk)/llvm-readelf"
    if [ -x "$p" ]; then printf '%s' "$p"; return 0; fi
    p="$(command -v readelf 2>/dev/null || true)"
    [ -n "$p" ] || return 1
    printf '%s' "$p"
}

# --- build-id ----------------------------------------------------------------
# Lê o NT_GNU_BUILD_ID. Sai pela string "Build ID:" do readelf, que é o mesmo
# formato que o tombstone do Android mostra entre parênteses.
symbols_build_id() {
    local so="$1" r
    r="$(symbols_readelf)" || return 0
    "$r" -n "$so" 2>/dev/null | awk '/Build ID:/ {print $3; exit}'
}

# --- registro ----------------------------------------------------------------
# symbols_add <so-NAO-STRIPPED> <symbols-root> <nome>
# Falha (exit != 0) se o .so não tiver .symtab: é o sintoma do build-id que
# mudou de volta, e descobrir isso aqui é melhor do que num crash de usuário.
symbols_add() {
    local so="$1" root="$2" name="$3" bid sections
    [ -f "$so" ] || { echo "symbols: .so ausente: $so" >&2; return 1; }
    bid="$(symbols_build_id "$so")"
    if [ -z "$bid" ] || [ "$bid" = "0" ]; then
        echo "symbols: sem NT_GNU_BUILD_ID em $so" >&2
        return 1
    fi
    sections="$("$(symbols_readelf)" -S "$so" 2>/dev/null)"
    # Sem `| grep -q`: o here-string escreve o conteudo antes de o grep rodar,
    # entao o grep e o unico processo do pipeline e nao ha produtor para levar
    # SIGPIPE. Com pipefail, o 141 do produtor viraria falha mesmo com o
    # .symtab presente. Ver tools/pipefail_grep_check.sh.
    if ! grep -q '\.symtab' <<<"$sections"; then
        echo "symbols: $name saiu sem .symtab — o build voltou a stripar e o" >&2
        echo "symbols: build-id não vai servir para nada. See jni/repro.mk." >&2
        return 1
    fi
    mkdir -p "$root/$bid"
    cp "$so" "$root/$bid/$name.so"
    printf '%s\t%s\t%s\n' "$bid" "$name" "$bid/$name.so" >>"$root/INDEX"
    printf '%s' "$bid"
}

# --- entrega (o que vai pro usuário) ----------------------------------------
# symbols_ship <src-NAO-STRIPPED.so> <dest-que-vai-pro-usuario> [symbols-root]
#
# O ndk-build agora sai NÃO-stripado (APP_STRIP_MODE := none em jni/repro.mk),
# porque sem .symtab/DWARF o .so de release é um offset morto. O preço é que
# TODO mundo que copia o .so do diretório de build passa a levar o binário
# gigante: o .bmod que o usuário baixa, o .so que o deploy joga no device, e o
# u_dump.so embutido nos assets do APK (que cresce ~1,6 MB por .so).
#
# Esta função é o ÚNICO lugar onde isso é feito, e ela se recusa a entregar um
# .so com símbolo. Todo consumidor de libs/arm64-v8a/*.so passa por aqui.
#
# Os três invariantes que ela checa, e por quê:
#   1. dest NÃO tem .symtab — o binário do usuário/device é stripped;
#   2. dest tem o MESMO build-id do src — o llvm-strip preserva a nota
#      NT_GNU_BUILD_ID, e é isso que faz o tombstone do device cruzar com os
#      símbolos guardados. Se um dia o strip mexer no build-id, o crash do
#      usuário deixa de ter símbolo e a etapa do gate tem que gritar;
#   3. dest é MENOR que src — strip que não rodou passa em 1 e 2 e falharia
#      em 3.
symbols_ship() {
    local src="$1" dest="$2" root="${3:-}" strip_bin bid_src bid_dst
    [ -f "$src" ] || { echo "symbols: .so de entrada ausente: $src" >&2; return 1; }
    strip_bin="$(symbols_strip_bin)"
    [ -n "$strip_bin" ] || {
        echo "symbols: llvm-strip ausente (instale o NDK ou ponha no PATH)" >&2
        return 1
    }
    # Guarda o não-stripado ANTES de gerar o stripped, e só quando pediram.
    if [ -n "$root" ]; then
        symbols_add "$src" "$root" "$(basename "$src" .so)" >/dev/null || return 1
    fi
    mkdir -p "$(dirname "$dest")"
    "$strip_bin" --strip-unneeded -o "$dest" "$src" || {
        echo "symbols: strip falhou: $src -> $dest" >&2
        return 1
    }
    # (1) sem símbolo no que vai pro usuário. Saída para arquivo, sem pipe:
    # `readelf | grep -q` mata o readelf com SIGPIPE e vira loteria (1 em 20).
    local sec
    sec="$(mktemp)"
    "$(symbols_readelf)" -S "$dest" >"$sec" 2>/dev/null || true
    if grep -q '\.symtab' "$sec"; then
        rm -f "$sec"
        echo "symbols: $dest AINDA tem .symtab — o .so do usuário/device não pode" >&2
        echo "symbols: carregar símbolo. Strip quebrado ou caminho errado." >&2
        return 1
    fi
    rm -f "$sec"
    # (2) mesmo build-id: é o que liga um crash do device aos símbolos guardados.
    bid_src="$(symbols_build_id "$src")"
    bid_dst="$(symbols_build_id "$dest")"
    if [ -n "$bid_src" ] && [ "$bid_src" != "$bid_dst" ]; then
        echo "symbols: o strip MUDOU o build-id de $dest ($bid_src -> $bid_dst)." >&2
        echo "symbols: o tombstone do device deixaria de cruzar com os símbolos." >&2
        return 1
    fi
    # (3) tem que ter encolhido; um strip que não rodou deixaria do mesmo tamanho.
    if [ "$(stat -c%s "$src")" = "$(stat -c%s "$dest")" ]; then
        echo "symbols: $dest ficou do mesmo tamanho que $src — o strip não fez nada." >&2
        return 1
    fi
    return 0
}

# --- consulta ----------------------------------------------------------------
# symbols_find <root> <build-id> [nome]  -> caminho do .so (ou lista de nomes)
#
# build-id VAZIO é erro, não curinga. Sem esta guarda, um frame de tombstone sem
# BuildId (Android 8-) caía em "$root//" — que é o diretório RAIZ, existia, e o
# `find` devolvia o primeiro .so guardado. O resultado era um crash da
# libil2cpp.so reportado como função do u_patch: pior que não simbolizar.
symbols_find() {
    local root="$1" bid="$2" name="${3:-}"
    if [ -z "$bid" ]; then return 1; fi
    if [ -n "$name" ]; then
        [ -f "$root/$bid/$name.so" ] || return 1
        printf '%s' "$root/$bid/$name.so"
        return 0
    fi
    [ -d "$root/$bid" ] || return 1
    find "$root/$bid" -name '*.so' -print | sort
}

# symbols_list <root>  -> "build-id  nome" de tudo que há guardado
symbols_list() {
    local root="$1"
    [ -f "$root/INDEX" ] || return 1
    sort -u "$root/INDEX"
}

