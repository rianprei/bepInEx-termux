LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := t_crash
LOCAL_SRC_FILES := t_crash_mod.cpp
# Sem Dobby, sem il2cpp: o ponto é MATAR o processo 2s depois de carregar, para
# exercitar o crashguard do loader (F1d).
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
