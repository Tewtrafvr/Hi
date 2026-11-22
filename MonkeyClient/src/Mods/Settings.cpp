// Settings implementation — see Settings.h.
#include "Settings.h"

#include "../Logging.h"

namespace Settings {

void Init() {
    // These two literals are the exact values the hex-patched libEclipse.so
    // writes in Settings::Init (background alpha 1.0, menu scale x0.875).
    backgroundColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    menuSize        = { 0.0875f, 0.875f, 0.6125f };
    dropMenu        = false;
    MonkeyLog::Print("Settings::Init — black opaque portrait menu, size %.4f/%.4f/%.4f",
                     menuSize.x, menuSize.y, menuSize.z);
}

void Reset() { Init(); }

} // namespace Settings
