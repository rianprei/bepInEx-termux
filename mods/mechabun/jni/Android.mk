LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := mechabun
LOCAL_SRC_FILES := mechabun_mod.cpp
# bc_mod_api.h eh o contrato publico do loader — mod so inclui esse header,
# nao linka contra o bc-poc nem contra Dobby (install_hook jah embrulha isso).
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../jni
include $(BUILD_SHARED_LIBRARY)
