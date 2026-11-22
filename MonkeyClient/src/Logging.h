#pragma once
// Logging: logcat + a fixed-size ring buffer.
// The ring buffer is what the CrashLogger dumps into crash files, so the last
// thing the mod did before a crash is always in the exported log.
#include <android/log.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <ctime>

#include "Config.h"

namespace MonkeyLog {

static constexpr int kRingSize = 128;
struct Line { long long ms; char text[256]; };
inline Line g_ring[kRingSize];
inline int  g_ring_pos = 0;

inline long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

inline void RingPush(const char* level, const char* fmt, va_list args) {
    Line& line = g_ring[g_ring_pos];
    g_ring_pos = (g_ring_pos + 1) % kRingSize;
    line.ms = NowMs();
    char body[sizeof(line.text) - 32];
    vsnprintf(body, sizeof(body), fmt, args);
    snprintf(line.text, sizeof(line.text), "[%s] %s", level, body);
}

// Formatted + logcat + ring buffer (use this one for startup milestones).
inline void Print(const char* fmt, ...) {
    va_list a; va_start(a, fmt);
    char msg[256]; vsnprintf(msg, sizeof(msg), fmt, a);
    va_end(a);
    __android_log_print(ANDROID_LOG_INFO, ANDROID_LOG_TAG, "%s", msg);
    va_list b; va_start(b, fmt);
    RingPush("info", fmt, b);
    va_end(b);
}

// Snapshot the ring buffer (oldest -> newest) into a caller-provided sink.
template <typename Fn>
inline void ForEachRecentLine(Fn&& fn) {
    for (int i = 0; i < kRingSize; ++i) {
        int idx = (g_ring_pos + i) % kRingSize;
        if (g_ring[idx].text[0] == '\0') continue;
        fn(g_ring[idx]);
    }
}

inline void ClearRing() {
    for (auto& l : g_ring) l.text[0] = '\0';
    g_ring_pos = 0;
}

} // namespace MonkeyLog
