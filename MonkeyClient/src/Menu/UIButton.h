#pragma once
// UIButton — a poke-able menu button.
//
// KEY BEHAVIOR ("it only highlights a button when you have it on"):
// the highlight quad exists only when the toggle state is ON. Action buttons
// (state == nullptr) never highlight. Off = plain near-black button, grey text.
#include "Menu/Theme.h"
#include "../IL2CPP/GameAPI.h"

namespace UIButton {

struct Button {
    void* go = nullptr;           // button root GameObject (scale 1)
    void* highlightGo = nullptr;  // highlight quad — active ONLY when toggled on
    void* textGo = nullptr;       // TextMesh child
    const char* label = nullptr;
    bool* state = nullptr;        // toggle buttons point at their Settings flag
    void (*onClick)() = nullptr;  // action buttons (no highlight)
    float halfW = 0.0f;           // poke half-extents (button-local, meters)
    float halfH = 0.0f;
    bool fingerWasIn = false;     // debounce: fire on poke ENTER only

    // Returns true on the frame the poke activates it.
    bool CheckPoke(const Game::Vector3& fingerWorld);
    void RefreshVisual();
};

} // namespace UIButton
