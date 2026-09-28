include $(call my-dir)/jni/Application.mk

# Build reproduzivel + simbolos preservados. Ver jni/repro.mk.
BEPINEX_REPRO_ROOT := $(abspath ../..)
include $(BEPINEX_REPRO_ROOT)/jni/repro.mk
