#pragma once
// MenuManager — the holdable Eclipse menu.
//
//  * Hold grip: menu attaches to that hand.
//  * Release grip: hides — OR, with the "Drop Menu" toggle on, detaches and
//    falls to the ground (that's the "when you let go it drops" behavior).
//  * Poke buttons with the right hand.
//  * Pages: Main / Settings. Settings holds the "Drop Menu" toggle.
namespace MenuManager {

// Create all menu objects. Requires Game::Init() first. Returns false on
// allocation failure (details in logcat).
bool Build();

// One frame of menu logic: hold input, drop physics, pokes, title effects.
void Update();

// Hide + destroy everything (optional, for unloaders).
void Shutdown();

} // namespace MenuManager
