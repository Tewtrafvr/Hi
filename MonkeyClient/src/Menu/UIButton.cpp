// UIButton implementation — see UIButton.h.
#include "UIButton.h"

#include "../Logging.h"

namespace UIButton {

bool Button::CheckPoke(const Game::Vector3& fingerWorld) {
    if (!go) return false;
    // Poke test in button-local space (buttons are scale-1 containers, so
    // local units are meters and the test is rotation-independent).
    Game::Vector3 local = Game::WorldToLocal(Game::TransformOf(go), fingerWorld);
    const float zHalf = 0.05f;
    bool inside =
        local.x > -halfW && local.x < halfW &&
        local.y > -halfH && local.y < halfH &&
        local.z > -zHalf && local.z < zHalf;

    // Rising-edge only: a finger resting on the button must NOT re-trigger
    // every frame. Fire when the finger ENTERS the button.
    if (inside && !fingerWasIn) {
        fingerWasIn = true;
        if (onClick) { onClick(); return true; }
        if (state) {
            *state = !*state;
            RefreshVisual();
            MonkeyLog::Print("button '%s' -> %s", label, *state ? "ON" : "OFF");
            return true;
        }
        return false;
    }
    if (!inside) fingerWasIn = false;
    return false;
}

void Button::RefreshVisual() {
    bool on = state && *state;
    // The ONLY highlight in the menu: shown while (and only while) toggled on.
    Game::SetActive(highlightGo, on);
    Game::Color c = on ? Theme::kTextOn : Theme::kTextOff;
    Game::SetTextColor(textGo, c);
}

} // namespace UIButton
