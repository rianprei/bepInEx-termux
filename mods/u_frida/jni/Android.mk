LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := u_frida
LOCAL_SRC_FILES := u_frida_mod.cpp
# Sem dobby aqui: este mod só escreve config + dlopen no gadget.
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
