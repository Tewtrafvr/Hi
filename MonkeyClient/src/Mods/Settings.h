#pragma once
// Settings state — shared by every page/effect. Init() reproduces the exact
// defaults found (and hex-patched) in the original client's Settings::Init.
namespace Settings {

// ---- look & feel ----
struct Vector3 { float x, y, z; };
struct Color   { float r, g, b, a; };

// Defaults = the Eclipse build values (libEclipse.so):
//   background alpha 0.95 -> 1.0 (opaque black)
//   menuSize (0.1, 1.0, 0.7) -> x0.875
inline Color  backgroundColor = { 0.0f, 0.0f, 0.0f, 1.0f };
inline Vector3 menuSize       = { 0.0875f, 0.875f, 0.6125f };

// ---- toggles (Settings page) ----
inline bool dropMenu        = false; // "Drop Menu": releasing grip drops the menu

void Init();          // apply defaults (call once at startup)
void Reset();         // back to defaults

} // namespace Settings
