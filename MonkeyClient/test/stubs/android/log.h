// Host-only stand-in for the Android NDK's <android/log.h>, used by the
// self-test so the template compiles on a desktop without the NDK.
#pragma once
#include <cstdio>
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_ERROR 6
inline int __android_log_print(int, const char*, const char*, ...) { return 0; }
