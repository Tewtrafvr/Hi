# MonkeyClient — Eclipse mod menu template (Meta Quest / Gorilla Tag)

Source template for a **holdable, world-space Quest mod menu** branded **Eclipse**,
matching the patched `libEclipse.so` build (black theme, smaller portrait menu,
highlight-only-when-on buttons) and adding the features that cannot be added by
hex-patching a compiled binary:

| Feature | Where |
|---|---|
| All-black opaque portrait menu (panel 0.0875 × 0.875 × 0.6125 — 12.5 % smaller than the original) | `src/Menu/Theme.h`, `src/Mods/Settings.cpp` (`Settings::Init`) |
| Menu title **Eclipse** | `src/Config.h` (`MENU_NAME`) |
| Buttons highlight **only when toggled on** | `src/Menu/UIButton.cpp` |
| **Drop Menu** toggle (Settings page) — release grip and the menu drops instead of hiding | `src/Menu/MenuManager.cpp` (`Update`) |
| Crash sections appended to **/storage/emulated/0/Download/eclipse_mod_crash.txt** (one file, every crash adds a section — same design as the soda_mod reference logger) | `src/Crash/CrashLogger.cpp` |
| Hold-to-open menu (grip), finger-poke buttons with edge-debounce | `src/Menu/MenuManager.cpp` |

## How the menu behaves (as implemented + self-tested)

* Hold **grip** → the menu pins in front of that hand (positioned once on grab,
  so the free hand can reach every button). Release → it hides — or, with
  **Drop Menu** on, it detaches and falls to the ground. Grab it again anywhere
  to re-pin it.
* Closing via **Close Menu** while still holding requires releasing the grip
  before it can re-open (no pop-back).
* Tabs `Main` / `Settings` highlight the active page; toggles highlight only
  while ON; action buttons (Close/Reset) never highlight.
* Crash handler catches SIGSEGV/SIGABRT/SIGBUS/SIGILL/SIGFPE/SIGTRAP and
  appends a `==== Eclipse CRASH ====` section to **eclipse_mod_crash.txt**:
  signal + name, fault address, time, mod module/base, pc/sp/lr, x0..x7, a
  frame-pointer backtrace, the last 128 mod log lines, `==== end ====`. One
  file keeps the crash history; past 512 KB it is rotated (`==== log rotated
  ====` marker). File candidates: public Downloads → `/sdcard/Download` → the
  app's external files dir → the app's internal files dir (derived from
  `/proc/self/cmdline`) → `SetFallbackDir()`. The handler is async-signal-safe
  (open/write/fsync only — no stdio streams or malloc, so it also runs when
  the crash is inside malloc), runs on a `sigaltstack` (stack-overflow
  SIGSEGVs are caught), guards nested faults with `_exit(sig+0x80)`, and
  re-raises so the OS still reports the crash. Modeled on the reference logger
  in `libshallowsmod_shadowhook.so` — with its bugs fixed: correct register
  offsets (the reference's `pc=` line actually prints `pstate` and its
  `x0..x7` line prints `x1..x8`), a fully initialized `sa_mask`, and a real
  `sigaltstack`.

## Layout

```
MonkeyClient/
├── CMakeLists.txt            aarch64 Android build (NDK) — no external deps
├── build.sh                  one-command build -> build/libMonkeyClient.so
├── extern/                   optional (only if you add native hooking libs)
├── test/selftest.cpp         host self-test (mock il2cpp backend)
└── src/
    ├── main.cpp              entry point: init, menu spawn, crash handler install, update loop
    ├── Config.h              menu name, sizes, colors, paths, keycodes
    ├── Logging.h             logcat + ring buffer (crash dumps include the last lines)
    ├── Crash/CrashLogger.*   signal handlers -> append sections to eclipse_mod_crash.txt
    ├── Menu/Theme.h          the Eclipse theme values (single source of truth)
    ├── Menu/UIButton.*       toggle buttons, highlight only when ON
    ├── Menu/MenuManager.*    holdable menu: title, pages, poke input, drop physics
    ├── Mods/Settings.*       settings state + Init() defaults (match the .so patches)
    └── IL2CPP/GameAPI.*      ALL game/il2cpp access, isolated here (public il2cpp C API)
```

## Building

1. Install Android NDK **r25+** and CMake 3.22+.
2. `./build.sh` → `build/libMonkeyClient.so` (no third-party libraries needed).
3. Side-load like any Quest mod (Horizon mod loader / your mod loader of choice).

## Integration checklist

- `src/IL2CPP/GameAPI.cpp` is the **only** file that touches the game. It
  resolves classes/methods through the public il2cpp C exports, so no resolver
  library is needed. Class/field names there (GorillaLocomotion.Player rig
  fields, UI primitives) match the dump layout used by the original client —
  adjust them if your dump differs.
- Controller input uses `UnityEngine.Input.GetKey` with joystick keycodes
  (`Config.h`); swap in a nicer input path there if your loader provides one.
- Crash file name and size cap are set in `Config.h` (`CRASH_LOG_FILE`,
  `CRASH_LOG_MAX_BYTES`). If the game has no external-storage access, the
  logger automatically falls back to the app's own files dirs and says so in
  logcat.

## Relation to libEclipse.so

`libEclipse.so` (the hex-patched build of the original client) and this template
agree on the same user-visible settings, expressed in source in
`Mods/Settings.cpp`:

```cpp
backgroundColor = Color(0, 0, 0, 1.0f);          // opaque black  (hex: alpha 0.95 -> 1.0)
menuSize        = Vector3(0.0875f, 0.875f, 0.6125f); // x0.875 smaller, portrait
```

## Self-test (host, no NDK needed)

The menu logic runs on the desktop against a mock il2cpp backend:

```
g++ -std=c++20 -I src -I test/stubs -o /tmp/selftest \
    test/selftest.cpp src/Menu/MenuManager.cpp src/Menu/UIButton.cpp \
    src/Mods/Settings.cpp src/Crash/CrashLogger.cpp
/tmp/selftest
```

It covers: grab/pin, Settings tab, Drop Menu (release → falls, stays visible),
page switching, close + close-lock, and crash-log export (the test forks two
children that dereference NULL and checks that BOTH sections were appended to
`eclipse_mod_crash.txt` with all expected fields). Note:
`test/stubs/android/log.h` is a one-file stand-in for the Android NDK header;
create it if you cloned this template without it.
