#ifndef U_PATCH_PKG_H
#define U_PATCH_PKG_H

#include <stdio.h>
#include "../../../jni/bc_loader.h"
#include "../../../jni/bc_process.h"

#define UP_PACKAGE_CAP BC_PROCESS_PACKAGE_CAP
#define UP_MODS_DIR_CAP 320

static inline enum bc_process_status upatch_package_prepare(
    const char *candidate, char *package, size_t package_size,
    char *mods_dir, size_t mods_dir_size) {
    if (!package || package_size == 0 || !mods_dir || mods_dir_size == 0)
        return BC_PROCESS_REJECT_PACKAGE;
    package[0] = '\0';
    mods_dir[0] = '\0';
    enum bc_process_status status =
        bc_process_copy_from_nice_name(candidate, package, package_size);
    if (status != BC_PROCESS_READY) return status;
    int n = snprintf(mods_dir, mods_dir_size, "%s/%s", BC_GENERIC_MODS_DIR, package);
    if (n <= 0 || (size_t)n >= mods_dir_size) {
        package[0] = '\0';
        mods_dir[0] = '\0';
        return BC_PROCESS_REJECT_PACKAGE_TOO_LONG;
    }
    return BC_PROCESS_READY;
}

#endif
