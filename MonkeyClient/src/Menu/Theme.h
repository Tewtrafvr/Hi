#pragma once
// The Eclipse theme — single source of truth.
// These values match the hex-patched libEclipse.so build:
//   * opaque black background (alpha 1.0, was 0.95)
//   * panel scale 0.875x of the original (smaller), still portrait.
#include "Config.h"
#include "../IL2CPP/GameAPI.h"

namespace Theme {

using Color = Game::Color; // single color type across the template
inline constexpr Color kBackground { 0.0f, 0.0f, 0.0f, 1.0f };

// Button highlight color — only shown when a toggle is ON.
inline constexpr Color kHighlight   { 0.25f, 0.25f, 0.25f, 1.0f };
inline constexpr Color kTextOff     { 0.62f, 0.62f, 0.62f, 1.0f }; // grey = off
inline constexpr Color kTextOn      { 1.0f,  1.0f,  1.0f,  1.0f }; // white = on
inline constexpr Color kTitleColor  { 1.0f,  1.0f,  1.0f,  1.0f };

// Menu panel scale: x = thickness, y = height, z = width.
// Original was (0.1, 1.0, 0.7); ours is 0.875x -> (0.0875, 0.875, 0.6125).
inline constexpr float kMenuSizeX = 0.1f    * kMenuScaleFactor;
inline constexpr float kMenuSizeY = 1.0f    * kMenuScaleFactor;
inline constexpr float kMenuSizeZ = 0.7f    * kMenuScaleFactor;

// Portrait check (compile-time): height must exceed width.
static_assert(kMenuSizeY > kMenuSizeZ, "menu panel must stay portrait");

} // namespace Theme
