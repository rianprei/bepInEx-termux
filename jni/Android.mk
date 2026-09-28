LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dobby
# O prebuilt passa por $(call bepinex_prebuilt,...): sem o DWARF que
# carrega o /home de quem o compilou. O .text nao muda. Ver jni/repro.mk.
LOCAL_SRC_FILES := $(call bepinex_prebuilt,jni/lib/$(TARGET_ARCH_ABI)/libdobby.a)
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := bc-poc
LOCAL_SRC_FILES := main.cpp companion.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)
LOCAL_STATIC_LIBRARIES := dobby
include $(BUILD_SHARED_LIBRARY)
