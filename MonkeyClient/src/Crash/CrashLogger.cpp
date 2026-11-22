// Crash logger — see CrashLogger.h.
//
// Design mirrors the reference logger found in libshallowsmod_shadowhook.so
// (its static `crash_export(int, siginfo*, void*)`): ONE fixed-name .txt file
// in Download, opened O_APPEND, every crash appends a "==== <MENU_NAME> CRASH ===="
// section with signal, fault address, time, pc/sp/lr, x0..x7, the mod's ring
// buffer and a "==== end ====" marker. Nested faults hit the g_inCrash guard
// and _exit(sig+0x80); the fault is re-raised with the default disposition so
// the OS still reports the crash.
//
// Fixes over the reference (which is why this one "actually works"):
//   * SA_ONSTACK is backed by a real sigaltstack — the reference would run
//     its handler on a corrupted stack after a stack-overflow SIGSEGV.
//   * sa_mask is fully sigfillset()ed — the reference leaves the tail of the
//     mask uninitialized (stack garbage decides which signals stay enabled).
//   * Registers are read through ucontext_t FIELDS. The reference hand-offsets
//     uc_mcontext as if it started at +0xB0 (bionic puts it at +0xA8), so its
//     "pc=" prints pstate and its "x0..x7" line prints x1..x8.
//   * The app dirs are derived from /proc/self/cmdline instead of hardcoding
//     one package name, and the whole prefix chain is pre-created at install.
// All handler I/O is async-signal-safe: open/write/fsync/close + snprintf
// (snprintf is signal-safe), no malloc, no stdio streams.
#include "CrashLogger.h"

#include <android/log.h>
#include <cstdarg>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

#include "../Config.h"
#include "../Logging.h"

namespace CrashLogger {

// ------------------------------------------------------- candidate paths
// Fixed-size, computed once in Install() so the handler only open()s.
static char g_paths[6][224];
static int  g_pathCount = 0;

static char g_selfName[64]  = "unknown";    // e.g. libEclipse.so
static uintptr_t g_selfBase = 0;            // load base of this .so

static volatile sig_atomic_t g_inCrash = 0; // nested-fault guard (g_inCrash)
static char g_fallbackDir[160] = "";        // SetFallbackDir override

struct SigDesc { int sig; const char* name; };
static const SigDesc kSignals[] = {
    { SIGSEGV, "SIGSEGV" }, { SIGBUS, "SIGBUS" },   { SIGABRT, "SIGABRT" },
    { SIGILL,  "SIGILL"  }, { SIGFPE, "SIGFPE" },   { SIGTRAP, "SIGTRAP" },
};

// --------------------------------------------------- async-signal-safe I/O
static void W(int fd, const char* s, size_t n) {
    while (n) {
        ssize_t r = write(fd, s, n);
        if (r <= 0) return;          // give up on error; never loop forever
        s += r; n -= (size_t)r;
    }
}
static void WS(int fd, const char* s) { W(fd, s, strlen(s)); }

// snprintf'd line -> write (snprintf itself is async-signal-safe).
static void WF(int fd, const char* fmt, ...) {
    char buf[512];
    va_list a; va_start(a, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, a);
    va_end(a);
    if (n > 0) W(fd, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
}

// -------------------------------------------------------------- path setup
// mkdir -p every prefix of the file's directory chain (like the reference).
static void MkDirChainOfFile(const char* filePath) {
    char buf[224];
    snprintf(buf, sizeof(buf), "%s", filePath);
    for (char* p = buf + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0777);        // EEXIST is fine
            *p = '/';
        }
    }
    mkdir(buf, 0777);
}

// /proc/self/cmdline -> package name -> the app's own always-writable dirs.
static void DeriveAppPaths() {
    char pkg[128] = "";
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd >= 0) {
        char raw[128] = "";
        ssize_t n = read(fd, raw, sizeof(raw) - 1);
        close(fd);
        if (n > 0) {
            raw[n] = '\0';
            snprintf(pkg, sizeof(pkg), "%s", raw);   // first NUL-terminated arg
            for (char* c = pkg; *c; ++c) if (*c < 32 || *c > 126) { *c = '\0'; break; }
        }
    }
    if (pkg[0]) {
        snprintf(g_paths[g_pathCount++], 224,
                 "/storage/emulated/0/Android/data/%s/files/%s", pkg, CRASH_LOG_FILE);
        snprintf(g_paths[g_pathCount++], 224,
                 "/data/data/%s/files/%s", pkg, CRASH_LOG_FILE);
        snprintf(g_paths[g_pathCount++], 224,
                 "/data/user/0/%s/files/%s", pkg, CRASH_LOG_FILE);
    }
    if (g_fallbackDir[0]) {
        size_t len = strlen(g_fallbackDir);
        if (g_fallbackDir[len - 1] == '/')
            snprintf(g_paths[g_pathCount++], 224, "%s%s", g_fallbackDir, CRASH_LOG_FILE);
        else
            snprintf(g_paths[g_pathCount++], 224, "%s/%s", g_fallbackDir, CRASH_LOG_FILE);
    }
}

static void ResolveSelf() {
    Dl_info info{};
    if (dladdr((void*)&ResolveSelf, &info) && info.dli_fname) {
        g_selfBase = (uintptr_t)info.dli_fbase;
        const char* slash = strrchr(info.dli_fname, '/');
        snprintf(g_selfName, sizeof(g_selfName), "%s", slash ? slash + 1 : info.dli_fname);
    }
}

// ============================== log rotation ==============================
// Like the reference's "==== log rotated ====": when the append file outgrows
// CRASH_LOG_MAX_BYTES, truncate it and mark the rotation. Checks every
// candidate (the one the handler will actually open is the first that
// exists/writable), not just candidate #0.
static void RotateIfNeeded() {
    for (int i = 0; i < g_pathCount; ++i) {
        struct stat st{};
        if (stat(g_paths[i], &st) != 0) continue;
        if ((unsigned long long)st.st_size <= CRASH_LOG_MAX_BYTES) continue;

        int fd = open(g_paths[i], O_WRONLY | O_TRUNC, 0666);
        if (fd >= 0) {
            WS(fd, "==== log rotated ====\n");
            fsync(fd);
            close(fd);
            MonkeyLog::Print("crash log rotated (was %lld bytes)",
                             (long long)st.st_size);
        }
        break;                                   // rotate the live file only
    }
}

// --------------------------------------------------------------- formatting
static void WriteBacktrace(int fd, ucontext_t* uc) {
    WS(fd, "\n==== backtrace ====\n");
#if defined(__aarch64__)
    uintptr_t fp = (uintptr_t)uc->uc_mcontext.regs[29];  // x29 frame pointer
    uintptr_t pc = (uintptr_t)uc->uc_mcontext.pc;
#elif defined(__x86_64__)
    uintptr_t fp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
    uintptr_t pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#else
    uintptr_t fp = 0, pc = 0;
#endif
    for (int i = 0; i < 64 && (pc || i == 0); ++i) {
        if (g_selfBase && pc >= g_selfBase && pc - g_selfBase < 0x10000000)
            WF(fd, "  #%-2d %s+0x%llx\n", i, g_selfName,
               (unsigned long long)(pc - g_selfBase));
        else
            WF(fd, "  #%-2d pc=0x%llx\n", i, (unsigned long long)pc);
        if (!fp || (fp & 7) || fp < 0x1000) break;
        uintptr_t next = *(uintptr_t*)fp;
        pc = *(uintptr_t*)(fp + 8);              // return address in the frame
        if (next <= fp || next - fp > 0x100000) break;   // bad chain -> stop
        fp = next;
    }
}

static void DumpRecentLogs(int fd) {
    WS(fd, "\n==== last log lines ====\n");
    MonkeyLog::ForEachRecentLine([&](const MonkeyLog::Line& line) {
        WS(fd, line.text);
        WS(fd, "\n");
    });
    WS(fd, "\n==== end ====\n");
}

// ---------------------------------------------------------------- handler
static void FaultHandler(int sig, siginfo_t* info, void* ctx) {
    if (g_inCrash) _exit(sig + 0x80);            // nested fault (like reference)
    g_inCrash = 1;

    int fd = -1;
    char usedPath[256] = "(none)";
    for (int i = 0; i < g_pathCount && fd < 0; ++i) {
        snprintf(usedPath, sizeof(usedPath), "%s", g_paths[i]);
        fd = open(usedPath, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd < 0) {                            // dirs vanished? mkdir + retry
            MkDirChainOfFile(usedPath);
            fd = open(usedPath, O_WRONLY | O_CREAT | O_APPEND, 0666);
        }
    }
    if (fd >= 0) {
        const char* sigName = strsignal(sig);    // same (%s) source as reference
        void* faultAddr = info ? info->si_addr : nullptr;
        time_t t = time(nullptr);

        WF(fd, "==== " MENU_NAME " CRASH ====\n");
        WF(fd, "signal=%d (%s)\n", sig, sigName ? sigName : "?");
        WF(fd, "fault_addr=%p\n", faultAddr);
        WF(fd, "time=%ld\n", (long)t);
        WF(fd, "module=%s base=0x%llx\n", g_selfName, (unsigned long long)g_selfBase);

        if (ctx) {
            ucontext_t* uc = (ucontext_t*)ctx;
#if defined(__aarch64__)
            const mcontext_t& mc = uc->uc_mcontext;
            WF(fd, "pc=0x%llx sp=0x%llx lr=0x%llx\n",
               (unsigned long long)mc.pc, (unsigned long long)mc.sp,
               (unsigned long long)mc.regs[30]);
            WF(fd, "x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx\n",
               (unsigned long long)mc.regs[0], (unsigned long long)mc.regs[1],
               (unsigned long long)mc.regs[2], (unsigned long long)mc.regs[3]);
            WF(fd, "x4=0x%llx x5=0x%llx x6=0x%llx x7=0x%llx\n",
               (unsigned long long)mc.regs[4], (unsigned long long)mc.regs[5],
               (unsigned long long)mc.regs[6], (unsigned long long)mc.regs[7]);
#elif defined(__x86_64__)
            const mcontext_t& mc = uc->uc_mcontext;
            WF(fd, "pc=0x%llx sp=0x%llx lr=0x%llx\n",
               (unsigned long long)mc.gregs[REG_RIP],
               (unsigned long long)mc.gregs[REG_RSP],
               (unsigned long long)mc.gregs[REG_RBP]);
            WF(fd, "x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx\n",
               (unsigned long long)mc.gregs[REG_RDI],
               (unsigned long long)mc.gregs[REG_RSI],
               (unsigned long long)mc.gregs[REG_RDX],
               (unsigned long long)mc.gregs[REG_RCX]);
            WF(fd, "x4=0x%llx x5=0x%llx x6=0x%llx x7=0x%llx\n",
               (unsigned long long)mc.gregs[REG_R8],
               (unsigned long long)mc.gregs[REG_R9],
               (unsigned long long)mc.gregs[REG_RAX],
               (unsigned long long)mc.gregs[REG_RBX]);
#endif
            WriteBacktrace(fd, uc);
            DumpRecentLogs(fd);
        } else {
            WS(fd, "\n==== end ====\n");
        }
        fsync(fd);                               // on disk before we die
        close(fd);
        __android_log_print(ANDROID_LOG_ERROR, ANDROID_LOG_TAG,
                            "!!!! CRASH — see %s !!!!", CRASH_LOG_FILE);
        MonkeyLog::Print("!!!! CRASH — see %s !!!!", CRASH_LOG_FILE);
    } else {
        __android_log_print(ANDROID_LOG_ERROR, ANDROID_LOG_TAG,
                            "crash (signal %d): could not open any crash file", sig);
    }

    // Chain to the default disposition like the reference:
    // signal(sig, SIG_DFL); raise(sig); return.
    signal(sig, SIG_DFL);
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, nullptr);   // raise() must be deliverable
    raise(sig);
}

// ------------------------------------------------------------- public API
void SetFallbackDir(const char* dir) {
    snprintf(g_fallbackDir, sizeof(g_fallbackDir), "%s", dir ? dir : "");
}

void Install() {
    ResolveSelf();

    // Candidates 1 + 2: the public Download dirs (stat-checked like the
    // reference's dir pick, and pre-created so the open succeeds later).
    for (const char* dir : { DOWNLOAD_LOG_DIR, DOWNLOAD_LOG_DIR_ALT }) {
        struct stat st{};
        if (stat(dir, &st) == 0 || mkdir(dir, 0777) == 0)
            snprintf(g_paths[g_pathCount++], 224, "%s/%s", dir, CRASH_LOG_FILE);
    }
    DeriveAppPaths();
    for (int i = 0; i < g_pathCount; ++i) MkDirChainOfFile(g_paths[i]);
    RotateIfNeeded();

    // Real alternate stack behind SA_ONSTACK (fixed size: SIGSTKSZ is not a
    // compile-time constant on newer glibc). Per-thread — installed on the
    // caller; other threads are still handled, just without altstack backup.
    static char altStack[128 * 1024];
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = FaultHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigfillset(&sa.sa_mask);
    for (const auto& d : kSignals) sigaction(d.sig, &sa, nullptr);

    MonkeyLog::Print("CrashLogger installed (append file %s, %d candidates, first: %s)",
                     CRASH_LOG_FILE, g_pathCount, g_pathCount ? g_paths[0] : "(none)");
}

} // namespace CrashLogger
