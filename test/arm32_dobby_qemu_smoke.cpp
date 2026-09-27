#include <cstdint>
#include <cstdio>

#include "dobby.h"

extern "C" __attribute__((noinline)) int target(int value) { return value + 5; }
using Target = int (*)(int);
static Target original_target;
extern "C" __attribute__((noinline)) int replacement(int value) {
    return original_target(value) * 2;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("target=%p replacement=%p\n", reinterpret_cast<void *>(target),
                reinterpret_cast<void *>(replacement));
    const int hook_status = DobbyHook(reinterpret_cast<void *>(target),
                                      reinterpret_cast<void *>(replacement),
                                      reinterpret_cast<void **>(&original_target));
    std::printf("DobbyHook=%d original=%p\n", hook_status,
                reinterpret_cast<void *>(original_target));
    if (hook_status != 0 || !original_target) return 1;
    const int actual = target(7);
    std::printf("DobbyHook=%d target(7)=%d\n", hook_status, actual);
    return hook_status == 0 && actual == 24 ? 0 : 1;
}
