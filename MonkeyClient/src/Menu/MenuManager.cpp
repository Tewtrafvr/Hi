// MenuManager implementation — see MenuManager.h.
#include <cstring>
#include "MenuManager.h"

#include "Theme.h"
#include "../Config.h"
#include "../Logging.h"
#include "../Mods/Settings.h"
#include "../IL2CPP/GameAPI.h"
#include "UIButton.h"

#include <chrono>
#include <vector>

using namespace Game;

namespace MenuManager {
namespace {

enum Held { kNone, kLeft, kRight };
enum Mode { kHidden, kHeld, kDropped };

Mode   g_mode = kHidden;
Held   g_held = kNone;
Vector3 g_dropVel = { 0, 0, 0 };
Vector3 g_dropPos = { 0, 0, 0 };

bool g_pageMain = true;
bool g_pageSettings = false;

// Set when the menu was closed by button; grip must be RELEASED before it can
// be re-grabbed (otherwise closing while holding would instantly re-open).
bool g_closeLock = false;

// Tab toggle states — highlight shows the ACTIVE page.
bool s_tabMain = true;
bool s_tabSettings = false;

Obj g_root = nullptr;          // empty container, scale 1 — everything parents to this
Obj g_panel = nullptr;         // black portrait cube (child of root)
Obj g_title = nullptr;         // MENU_NAME TextMesh object
Obj g_pageMainGo = nullptr;    // empty page containers
Obj g_pageSettingsGo = nullptr;

// Button storage: filled ONCE in Build() and never reallocated afterwards,
// so the page/tab pointers below stay valid for the process lifetime.
std::vector<UIButton::Button> g_buttons;
std::vector<UIButton::Button*> g_mainButtons;
std::vector<UIButton::Button*> g_settingsButtons;
std::vector<UIButton::Button*> g_tabButtons;

std::chrono::steady_clock::time_point g_lastTick = std::chrono::steady_clock::now();

// ------------------------------------------------------------- actions
void ActionClose() {
    g_mode = kHidden;
    g_closeLock = true;
    SetActive(g_root, false);
    MonkeyLog::Print("menu closed");
}
void ActionResetSettings() {
    Settings::Reset();
    for (auto& b : g_buttons) b.RefreshVisual();
}
void SwitchToMain()   { g_pageMain = true;  g_pageSettings = false;
                        s_tabMain = true;   s_tabSettings = false;
                        SetActive(g_pageMainGo, true); SetActive(g_pageSettingsGo, false);
                        for (auto* b : g_tabButtons) b->RefreshVisual(); }
void SwitchToSettings(){ g_pageMain = false; g_pageSettings = true;
                        s_tabMain = false;  s_tabSettings = true;
                        SetActive(g_pageMainGo, false); SetActive(g_pageSettingsGo, true);
                        for (auto* b : g_tabButtons) b->RefreshVisual(); }

// ------------------------------------------------------------- build helpers
struct Visual { Obj go, highlight, text; };

// One button: empty container (scale 1) + visual cube + highlight quad + label.
Visual MakeButtonVisual(Obj parent, const char* label, float y, float halfW, float halfH) {
    Visual v;
    v.go = CreateEmptyObject();
    SetParent(TransformOf(v.go), TransformOf(parent));
    SetLocalPosition(TransformOf(v.go), { 0, y, 0 });

    // Base cube (near-black, slightly in front of the panel).
    Obj cube = CreatePrimitive(kPrimitiveCube);
    SetParent(TransformOf(cube), TransformOf(v.go));
    SetLocalScale(TransformOf(cube), { halfW * 2.f, halfH * 2.f, 0.01f });
    SetLocalPosition(TransformOf(cube), { 0, 0, -0.012f });
    SetRendererColor(cube, { 0.05f, 0.05f, 0.05f, 1.0f });

    // Highlight quad — hidden until the toggle turns ON.
    v.highlight = CreatePrimitive(kPrimitiveCube);
    SetParent(TransformOf(v.highlight), TransformOf(v.go));
    SetLocalScale(TransformOf(v.highlight), { halfW * 2.06f, halfH * 2.2f, 0.008f });
    SetLocalPosition(TransformOf(v.highlight), { 0, 0, -0.02f });
    SetRendererColor(v.highlight, { Theme::kHighlight.r, Theme::kHighlight.g,
                                    Theme::kHighlight.b, Theme::kHighlight.a });
    SetActive(v.highlight, false); // <- "highlight only when it's on"

    // Label (TextMesh; scale 1 so glyphs stay undistorted).
    v.text = CreatePrimitive(kPrimitiveCube);
    SetParent(TransformOf(v.text), TransformOf(v.go));
    SetLocalScale(TransformOf(v.text), { 1, 1, 1 });
    SetLocalPosition(TransformOf(v.text), { 0, 0, -0.03f });
    AddTextMesh(v.text, label, 48, Theme::kTextOff);
    SetTextAnchorCenter(v.text);
    return v;
}

void AddButton(Visual v, const char* label, bool* state, void (*action)(),
               float halfW, float halfH, std::vector<UIButton::Button*>& page) {
    UIButton::Button b;
    b.go = v.go;
    b.highlightGo = v.highlight;
    b.textGo = v.text;
    b.label = label;
    b.state = state;
    b.onClick = action;
    b.halfW = halfW;
    b.halfH = halfH;
    g_buttons.push_back(b);
    page.push_back(&g_buttons.back());
}

Vector3 HoldOffset(Trans hand) {
    // Menu pins slightly in front of the holding hand when grabbed.
    Vector3 fwd = Forward(hand);
    Vector3 p = Position(hand);
    return { p.x + fwd.x * 0.18f, p.y + fwd.y * 0.18f, p.z + fwd.z * 0.18f };
}

} // namespace

// ------------------------------------------------------------------ Build
bool Build() {
    g_buttons.reserve(16); // stable addresses before any push_back

    // Root container (scale 1 — children keep their own proportions).
    g_root = CreateEmptyObject();
    if (!g_root) return false;
    SetActive(g_root, false);

    // Black portrait panel (Theme holds the exact 0.875x values).
    g_panel = CreatePrimitive(kPrimitiveCube);
    SetParent(TransformOf(g_panel), TransformOf(g_root));
    SetLocalScale(TransformOf(g_panel),
                  { Theme::kMenuSizeX, Theme::kMenuSizeY, Theme::kMenuSizeZ });
    SetRendererColor(g_panel, { Theme::kBackground.r, Theme::kBackground.g,
                                Theme::kBackground.b, Theme::kBackground.a });

    // Title — MENU_NAME (see Config.h).
    g_title = CreatePrimitive(kPrimitiveCube);
    SetParent(TransformOf(g_title), TransformOf(g_root));
    SetLocalScale(TransformOf(g_title), { 1, 1, 1 });
    SetLocalPosition(TransformOf(g_title), { 0, 0.37f, -0.02f });
    // Keep long names inside the portrait panel: 96 fits the original 7-char
    // title, so shrink the character size in proportion for anything longer.
    int titleSize = 96;
    const size_t titleLen = strlen(MENU_NAME);
    if (titleLen > 7) titleSize = static_cast<int>(96.0f * 7.0f / static_cast<float>(titleLen));
    AddTextMesh(g_title, MENU_NAME, titleSize, Theme::kTitleColor);
    SetTextAnchorCenter(g_title);

    // Tabs.
    Visual vTabMain     = MakeButtonVisual(g_root, "Main",     0.29f, 0.13f, 0.025f);
    SetLocalPosition(TransformOf(vTabMain.go), { -0.15f, 0.29f, 0 });
    Visual vTabSettings = MakeButtonVisual(g_root, "Settings", 0.29f, 0.13f, 0.025f);
    SetLocalPosition(TransformOf(vTabSettings.go), {  0.15f, 0.29f, 0 });

    // Pages.
    g_pageMainGo = CreateEmptyObject();
    SetParent(TransformOf(g_pageMainGo), TransformOf(g_root));
    g_pageSettingsGo = CreateEmptyObject();
    SetParent(TransformOf(g_pageSettingsGo), TransformOf(g_root));

    // Main page buttons.
    AddButton(MakeButtonVisual(g_pageMainGo, "Close Menu", 0.10f, 0.275f, 0.03f),
              "Close Menu", nullptr, ActionClose, 0.275f, 0.03f, g_mainButtons);
    AddButton(MakeButtonVisual(g_pageMainGo, "Reset Settings", 0.00f, 0.275f, 0.03f),
              "Reset Settings", nullptr, ActionResetSettings, 0.275f, 0.03f, g_mainButtons);

    // Settings page buttons — "Drop Menu" and "Reset Settings".
    AddButton(MakeButtonVisual(g_pageSettingsGo, "Drop Menu", 0.05f, 0.275f, 0.03f),
              "Drop Menu", &Settings::dropMenu, nullptr, 0.275f, 0.03f,
              g_settingsButtons);
    AddButton(MakeButtonVisual(g_pageSettingsGo, "Reset Settings", -0.05f, 0.275f, 0.03f),
              "Reset Settings", nullptr, ActionResetSettings, 0.275f, 0.03f,
              g_settingsButtons);

    // Tab buttons.
    AddButton(vTabMain, "Main", &s_tabMain, nullptr, 0.13f, 0.025f, g_tabButtons);
    AddButton(vTabSettings, "Settings", &s_tabSettings, nullptr, 0.13f, 0.025f, g_tabButtons);

    for (auto& b : g_buttons) b.RefreshVisual();
    SwitchToMain();

    MonkeyLog::Print("MenuManager built — '%s', panel %.4f x %.4f x %.4f (portrait, 0.875x)",
                     MENU_NAME, Theme::kMenuSizeX, Theme::kMenuSizeY, Theme::kMenuSizeZ);
    return true;
}

// ------------------------------------------------------------------ Update
void Update() {
    if (!g_root) return;
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g_lastTick).count();
    g_lastTick = now;
    if (dt <= 0.f || dt > 0.5f) dt = 1.f / 90.f;

    const bool leftGrip  = GripHeld(true);
    const bool rightGrip = GripHeld(false);

    // ------- poke input (right fingertip) -------
    // The menu is pinned 0.18 m in front of the grabbing hand, so the
    // fingertip reaches the panel plane when the hand is still.
    Trans right = RightHand();
    if (right && g_mode != kHidden) {
        Vector3 finger = Position(right);
        Vector3 fwd = Forward(right);
        finger.x += fwd.x * 0.18f;
        finger.y += fwd.y * 0.18f;
        finger.z += fwd.z * 0.18f;
        auto& page = g_pageMain ? g_mainButtons : g_settingsButtons;
        for (auto* b : page) b->CheckPoke(finger);
        for (auto* b : g_tabButtons) b->CheckPoke(finger);
        if (g_pageMain != s_tabMain) SwitchToMain();
        if (g_pageSettings != s_tabSettings) SwitchToSettings();
    }

    // ------- hold / drop -------
    // The menu PINS in front of the grabbing hand (positioned once on grab) so
    // the free hand can move to poke any button. Release hides it — or, with
    // "Drop Menu" on, it detaches and falls.
    switch (g_mode) {
        case kHidden:
            if ((leftGrip || rightGrip) && !g_closeLock) {
                g_held = leftGrip ? kLeft : kRight;
                g_mode = kHeld;
                Trans hand = (g_held == kLeft) ? LeftHand() : RightHand();
                if (hand) SetPosition(g_root, HoldOffset(hand));
                SetActive(g_root, true);
            } else if (!leftGrip && !rightGrip) {
                g_closeLock = false; // fresh grip now allowed again
            }
            break;

        case kHeld: {
            bool gripStill = (g_held == kLeft) ? leftGrip : rightGrip;
            if (!gripStill) {
                // ---- "when you let go it drops" ----
                if (Settings::dropMenu) {
                    g_dropPos = Position(g_root);
                    g_dropVel = { 0, 0, 0 };
                    g_mode = kDropped;
                    MonkeyLog::Print("menu dropped (Drop Menu on)");
                } else {
                    ActionClose();
                }
            }
            break;
        }

        case kDropped: {
            // Ballistic fall until it reaches ground level (approx: head height
            // minus average player height). Grabbing it again re-pins it.
            if (leftGrip || rightGrip) {
                g_held = leftGrip ? kLeft : kRight;
                g_mode = kHeld;
                Trans hand = (g_held == kLeft) ? LeftHand() : RightHand();
                if (hand) SetPosition(g_root, HoldOffset(hand));
                break;
            }
            g_dropVel.y -= 9.81f * dt;
            g_dropPos.x += g_dropVel.x * dt;
            g_dropPos.y += g_dropVel.y * dt;
            g_dropPos.z += g_dropVel.z * dt;
            Trans head = Head();
            float groundY = head ? Position(head).y - 1.4f : g_dropPos.y;
            if (g_dropPos.y <= groundY) {
                g_dropPos.y = groundY;
                g_dropVel = { 0, 0, 0 };
            }
            SetPosition(g_root, g_dropPos);
            break;
        }
    }
}

// ------------------------------------------------------------------ Shutdown
void Shutdown() {
    if (g_root) SetActive(g_root, false);
    g_root = g_panel = g_title = g_pageMainGo = g_pageSettingsGo = nullptr;
    g_buttons.clear(); g_mainButtons.clear(); g_settingsButtons.clear(); g_tabButtons.clear();
    g_mode = kHidden;
}

} // namespace MenuManager
