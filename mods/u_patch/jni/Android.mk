LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dobby
LOCAL_SRC_FILES := ../../../jni/lib/$(TARGET_ARCH_ABI)/libdobby.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := u_patch
LOCAL_SRC_FILES := u_patch_mod.cpp
# Motor declarativo (F4): parser + emissores arm64 são headers puros
# (u_patch_parse.h, u_patch_arm64.h), testados no host. Aqui só Dobby.
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../../jni
LOCAL_STATIC_LIBRARIES := dobby
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
