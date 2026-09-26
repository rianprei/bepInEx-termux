LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := u_dump
LOCAL_SRC_FILES := u_dump_mod.cpp
# Mod autonomo do caminho generico (mods/<pkg>/): nao usa bc_mod_api.h.
# Scanner so le API il2cpp — sem hook, sem dobby.
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../jni
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
