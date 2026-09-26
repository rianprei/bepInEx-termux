// u_frida_config.h — parte pura do u_frida (F11): nomes de arquivo e
// montagem do config JSON do gadget. Header-only, sem dependência de
// plataforma: o teste host inclui direto.
//
// Regra de descoberta do config (fonte: frida-core/lib/gadget/gadget.vala,
// derive_config_path_from_file_path): <dir>/<stem>.config, onde stem é o
// nome do binário sem a última extensão. Por isso o binário se chama
// frida-gadget.bin (SEM .so — senão o loader daria dlopen sozinho, sem
// config, e o gadget travaria o jogo em modo listen/wait) e o config é
// frida-gadget.config ao lado dele.
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>

#define UF_GADGET_FILE "frida-gadget.bin"
#define UF_CONFIG_FILE "frida-gadget.config"

// É mod .js? *.js, menos oculto/subpasta. *.js.off não termina em .js,
// então já cai fora pela regra abaixo (igual C1 dos .so/.patch).
static inline bool uf_is_js_mod(const char *name) {
    if (name == nullptr || name[0] == '\0' || name[0] == '.') return false;
    if (strchr(name, '/') != nullptr) return false;
    size_t n = strlen(name);
    if (n < 4) return false;
    return strcmp(name + n - 3, ".js") == 0;
}

// Config modo script-directory: carrega TODO *.js do moddir, sem filtro
// (o gadget só existe dentro do processo desse jogo mesmo). on_change
// ignore = determinístico (Manager atualiza + reinicia o jogo).
static inline int uf_build_config(char *out, size_t outsz, const char *moddir) {
    return snprintf(out, outsz,
                    "{\"interaction\":{\"type\":\"script-directory\",\"path\":\"%s\",\"on_change\":\"ignore\"}}",
                    moddir);
}
