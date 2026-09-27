// Identidade e elegibilidade do processo Android antes do loader tocar no app.
// O pacote vem preferencialmente do basename de app_data_dir; nice_name é
// fallback e pode ser "com.exemplo:unity". Processos do mesmo pacote continuam
// elegíveis porque a engine pode estar carregada só em um processo secundário.
#ifndef BC_PROCESS_H
#define BC_PROCESS_H

#include <stddef.h>
#include <string.h>

#define BC_PROCESS_PACKAGE_CAP 256

enum bc_process_status {
    BC_PROCESS_READY,
    BC_PROCESS_SKIP_CHILD_ZYGOTE,
    BC_PROCESS_SKIP_APP_ZYGOTE,
    BC_PROCESS_SKIP_ISOLATED_UID,
    BC_PROCESS_REJECT_PACKAGE,
    BC_PROCESS_REJECT_PACKAGE_TOO_LONG
};

static inline bool bc_process_is_isolated_uid(int uid) {
    if (uid < 0) return false;
    const int app_id = uid % 100000;
    return app_id >= 90000 && app_id <= 99999;
}

static inline bool bc_process_is_app_zygote_name(const char *nice_name) {
    if (!nice_name) return false;
    const char *separator = strchr(nice_name, ':');
    const char *process_name = separator ? separator + 1 : nice_name;
    return strncmp(process_name, "app_zygote", 10) == 0;
}

static inline bool bc_process_package_valid(const char *value, size_t len) {
    if (!value || len == 0) return false;
    if ((len == 1 && value[0] == '.') ||
        (len == 2 && value[0] == '.' && value[1] == '.')) return false;
    for (size_t i = 0; i < len; i++) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
    }
    return true;
}

static inline bool bc_process_copy_package(const char *value, size_t len,
                                           char *out, size_t out_size) {
    if (!out || out_size == 0 || len >= out_size ||
        !bc_process_package_valid(value, len)) return false;
    memcpy(out, value, len);
    out[len] = '\0';
    return true;
}

static inline enum bc_process_status bc_process_copy_from_app_data(
    const char *app_data_dir, char *out, size_t out_size) {
    if (!app_data_dir || !*app_data_dir) return BC_PROCESS_REJECT_PACKAGE;
    const char *leaf = strrchr(app_data_dir, '/');
    leaf = leaf ? leaf + 1 : app_data_dir;
    if (!*leaf) return BC_PROCESS_REJECT_PACKAGE;
    const size_t len = strlen(leaf);
    if (len >= out_size) return BC_PROCESS_REJECT_PACKAGE_TOO_LONG;
    return bc_process_copy_package(leaf, len, out, out_size)
        ? BC_PROCESS_READY : BC_PROCESS_REJECT_PACKAGE;
}

static inline enum bc_process_status bc_process_copy_from_nice_name(
    const char *nice_name, size_t nice_name_len, char *out, size_t out_size) {
    if (!nice_name || nice_name_len == 0) return BC_PROCESS_REJECT_PACKAGE;
    const char *separator = (const char *)memchr(nice_name, ':', nice_name_len);
    const size_t len = separator ? (size_t)(separator - nice_name) : nice_name_len;
    if (len >= out_size) return BC_PROCESS_REJECT_PACKAGE_TOO_LONG;
    return bc_process_copy_package(nice_name, len, out, out_size)
        ? BC_PROCESS_READY : BC_PROCESS_REJECT_PACKAGE;
}

static inline enum bc_process_status bc_process_copy_from_nice_name(
    const char *nice_name, char *out, size_t out_size) {
    return nice_name
        ? bc_process_copy_from_nice_name(nice_name, strlen(nice_name), out, out_size)
        : BC_PROCESS_REJECT_PACKAGE;
}

static inline enum bc_process_status bc_process_select(
    int uid, bool is_child_zygote, const char *nice_name,
    const char *app_data_dir, char *package, size_t package_size) {
    if (!package || package_size == 0) return BC_PROCESS_REJECT_PACKAGE;
    package[0] = '\0';

    if (is_child_zygote) return BC_PROCESS_SKIP_CHILD_ZYGOTE;
    if (bc_process_is_app_zygote_name(nice_name)) return BC_PROCESS_SKIP_APP_ZYGOTE;
    if (bc_process_is_isolated_uid(uid)) return BC_PROCESS_SKIP_ISOLATED_UID;

    enum bc_process_status status =
        bc_process_copy_from_app_data(app_data_dir, package, package_size);
    if (status == BC_PROCESS_READY || status == BC_PROCESS_REJECT_PACKAGE_TOO_LONG)
        return status;
    return bc_process_copy_from_nice_name(nice_name, package, package_size);
}

static inline const char *bc_process_status_name(enum bc_process_status status) {
    switch (status) {
        case BC_PROCESS_READY: return "elegível";
        case BC_PROCESS_SKIP_CHILD_ZYGOTE: return "child zygote";
        case BC_PROCESS_SKIP_APP_ZYGOTE: return "app_zygote";
        case BC_PROCESS_SKIP_ISOLATED_UID: return "UID isolado";
        case BC_PROCESS_REJECT_PACKAGE: return "pacote inválido/ausente";
        case BC_PROCESS_REJECT_PACKAGE_TOO_LONG: return "nome de pacote longo demais";
    }
    return "estado de processo inválido";
}

#endif
