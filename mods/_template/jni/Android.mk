LOCAL_PATH := $(call my-dir)

# Template gerado por tools/new_mod.sh: __MOD_ID__ é trocado pelo id do mod.
# Não builda como está (LOCAL_MODULE inválido de propósito).
include $(CLEAR_VARS)
LOCAL_MODULE := __MOD_ID__
LOCAL_SRC_FILES := mod.cpp
# Mod autônomo do caminho genérico (mods/<pkg>/): só o SDK comum.
# Pra hookar: copie o bloco do dobby pré-built do mods/sa2ammo/jni/Android.mk
# e adicione LOCAL_STATIC_LIBRARIES := dobby.
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
