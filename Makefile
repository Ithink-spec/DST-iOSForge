include $(THEOS)/makefiles/common.mk

TWEAK_NAME = DSTMods
DSTMods_FILES = Sources/DSTMods.mm
DSTMods_CXXFLAGS = -std=c++17 -fobjc-arc
DSTMods_FRAMEWORKS = Foundation

include $(THEOS_MAKE_PATH)/tweak.mk
