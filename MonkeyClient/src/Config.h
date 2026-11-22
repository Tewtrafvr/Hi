#pragma once
// Central configuration for the TewtrafvrsPrivateMenu menu (MonkeyClient template).

#define MENU_NAME       "TewtrafvrsPrivateMenu"
#define MENU_VERSION    "1.0.0"

// ---- Theme (see Menu/Theme.h for the full values) ----
// Panel scale = 0.875x the original client: smaller AND portrait (height > width).
static constexpr float kMenuScaleFactor = 0.875f;

// ---- Input (UnityEngine.Input joystick keycodes) ----
// Unity KeyCode enum: JoystickButton0 = 350, so JoystickButton4 = 354, 5 = 355.
// On Quest Touch controllers 4/5 map to the left/right grips.
// VERIFY: swap if you prefer opening with X/A or your input path differs.
static constexpr int kKeyLeftGrip  = 354;  // KeyCode.JoystickButton4
static constexpr int kKeyRightGrip = 355;  // KeyCode.JoystickButton5

// ---- Crash log export ----
// "when the target app crashes make it export logs to a folder in Downloads".
// One append-mode file collects every crash as a "==== Eclipse CRASH ===="
// section (same design as the soda_mod reference logger). Candidates, in
// order: DOWNLOAD_LOG_DIR + file, DOWNLOAD_LOG_DIR_ALT + file, the app's
// external files dir, the app's internal files dir (both derived from
// /proc/self/cmdline), then SetFallbackDir(). Rotated past CRASH_LOG_MAX_BYTES.
#define CRASH_LOG_FILE       "eclipse_mod_crash.txt"
#define CRASH_LOG_MAX_BYTES  (512 * 1024)
#define DOWNLOAD_LOG_DIR     "/storage/emulated/0/Download"
#define DOWNLOAD_LOG_DIR_ALT "/sdcard/Download"  // fallback mount path
#define ANDROID_LOG_TAG      "MonkeyClient"

// Poke input: fingertip radius for button presses (world units).
static constexpr float kPokeRadius = 0.02f;
