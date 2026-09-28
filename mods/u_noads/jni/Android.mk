LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dobby
# Sem o DWARF do prebuilt (ver jni/repro.mk): o release nao leva o
# caminho home de quem compilou o Dobby. O .text nao muda.
LOCAL_SRC_FILES := $(call bepinex_prebuilt,jni/lib/$(TARGET_ARCH_ABI)/libdobby.a)
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := u_noads
LOCAL_SRC_FILES := u_noads_mod.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../jni
LOCAL_STATIC_LIBRARIES := dobby
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
