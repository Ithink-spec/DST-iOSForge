#!/bin/sh
# Run on a Mac with Xcode installed.
set -e
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
xcrun clang -arch arm64 -isysroot "$SDK" -miphoneos-version-min=14.0 \
  -dynamiclib -O2 -fvisibility=hidden \
  -install_name @executable_path/Frameworks/FWMods.dylib \
  -o FWMods.dylib fwmods.c
codesign -f -s - FWMods.dylib      # or re-sign with your own certificate
file FWMods.dylib
