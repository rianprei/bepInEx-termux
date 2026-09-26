LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := u_frida
LOCAL_SRC_FILES := u_frida_mod.cpp
# Sem dobby aqui: este mod só confere .js + bin + config (modo script) e dá dlopen no gadget.
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
