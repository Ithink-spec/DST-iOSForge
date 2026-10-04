include $(THEOS)/makefiles/common.mk

ARCHS = arm64
TARGET = iphone:clang:latest:15.0

TWEAK_NAME = DSTMods

DSTMods_FILES = Sources/DSTMods.mm
DSTMods_CXXFLAGS = -std=c++17
DSTMods_CCFLAGS = -std=c++17
DSTMods_FRAMEWORKS = Foundation

include $(THEOS_MAKE_PATH)/tweak.mk
