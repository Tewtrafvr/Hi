// MonkeyClient entry point.
//
// The mod .so is dlopen'd by the mod loader; a constructor spawns a worker
// thread that waits for the game's il2cpp runtime, then builds and drives the
// Eclipse menu at ~90 Hz. For loaders that can call mods per-frame, export
// MonkeyClient_Update() (below) and hook that instead.
#include <chrono>
#include <csignal>
#include <thread>

#include "Config.h"
#include "Logging.h"
#include "Crash/CrashLogger.h"
#include "IL2CPP/GameAPI.h"
#include "Mods/Settings.h"
#include "Menu/MenuManager.h"

namespace {

void UpdateThread() {
    // Wait for the game to finish loading il2cpp (up to ~2 minutes).
    bool ready = false;
    for (int i = 0; i < 1200 && !ready; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!Game::AttachCurrentThread()) continue;
        ready = Game::Init();
    }
    if (!ready) {
        MonkeyLog::Print("il2cpp never became ready — menu disabled");
        return;
    }

    Settings::Init();
    if (!MenuManager::Build()) {
        MonkeyLog::Print("MenuManager::Build failed — menu disabled");
        return;
    }
    MonkeyLog::Print("%s v%s running", MENU_NAME, MENU_VERSION);

    // ~90 Hz menu loop.
    while (true) {
        MenuManager::Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(11));
    }
}

} // namespace

// Loader integration for frame-hook based loaders: call this every frame.
extern "C" __attribute__((visibility("default")))
void MonkeyClient_Update() {
    MenuManager::Update();
}

// dlopen entry: install crash logging first (before anything can go wrong),
// then start the menu worker.
__attribute__((constructor))
static void MonkeyClient_Main() {
    CrashLogger::Install();   // dir fallbacks are derived from /proc/self/cmdline
    MonkeyLog::Print("MonkeyClient loading — menu '%s' (black portrait theme)", MENU_NAME);
    std::thread(UpdateThread).detach();
}
