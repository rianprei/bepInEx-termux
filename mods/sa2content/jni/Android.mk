LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dobby
# Sem o DWARF do prebuilt (ver jni/repro.mk): o release nao leva o
# caminho home de quem compilou o Dobby. O .text nao muda.
LOCAL_SRC_FILES := $(call bepinex_prebuilt,jni/lib/$(TARGET_ARCH_ABI)/libdobby.a)
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := sa2content
LOCAL_SRC_FILES := sa2content_mod.cpp
# Mod autonomo do caminho generico (mods/<pkg>/): nao usa bc_mod_api.h,
# so dobby.h pro hook.
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../jni
LOCAL_STATIC_LIBRARIES := dobby
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
