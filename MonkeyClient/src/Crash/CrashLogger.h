#pragma once
// Crash logger (modeled on the reference logger in libshallowsmod_shadowhook.so):
// ONE append-mode file collects every crash as a "==== Eclipse CRASH ===="
// section when the target app dies (SIGSEGV/SIGABRT/SIGBUS/SIGILL/SIGFPE/SIGTRAP).
//
// Section content: signal + name, fault address, epoch time, mod module/base,
// pc/sp/lr, x0..x7, a frame-pointer backtrace, and the last 128 mod log lines,
// ending with "==== end ====". The file grows across crashes and is rotated
// ("==== log rotated ====" marker) once it exceeds CRASH_LOG_MAX_BYTES.
//
// File candidates, tried in order until one opens (all pre-created at Install):
//   1. /storage/emulated/0/Download/CRASH_LOG_FILE   (public Downloads)
//   2. /sdcard/Download/CRASH_LOG_FILE               (same mount, other path)
//   3. /storage/emulated/0/Android/data/<pkg>/files/...
//   4. /data/data/<pkg>/files/...                    (always writable)
//   5. /data/user/0/<pkg>/files/...                  (same inode as 4)
//   6. the SetFallbackDir() override, if one was given
// <pkg> is read from /proc/self/cmdline at Install().
//
// Handler properties: async-signal-safe (open/write/fsync + snprintf — no
// stdio streams, no malloc), SA_ONSTACK with a real sigaltstack (stack-overflow
// SIGSEGVs are caught), sigfillset()ed mask, nested-fault guard
// (_exit(sig+0x80)), and signal(sig, SIG_DFL) + raise() so the OS still
// reports the crash.

namespace CrashLogger {

// Install signal handlers + sigaltstack + pre-create the candidate dirs.
// Call once during mod init, on the thread that stays alive.
void Install();

// Optional explicit last-resort directory (e.g. getFilesDir() passed through
// JNI). Call BEFORE Install(); pass nullptr to clear.
void SetFallbackDir(const char* dir);

} // namespace CrashLogger
