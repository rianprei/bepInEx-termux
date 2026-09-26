LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := thunk_exec
LOCAL_SRC_FILES := thunk_exec.c up_shim.cpp
# Puxa o emissor real (u_patch_arm64.h) — nada duplicado, o teste executa
# exatamente o código que o mod usa no jogo.
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../../mods/u_patch/jni
# Executável estático: roda no device sem depender do linker namespace do
# Zygote e roda no host via qemu-aarch64 sem instalar bionic do sistema.
LOCAL_LDFLAGS := -static
include $(BUILD_EXECUTABLE)
