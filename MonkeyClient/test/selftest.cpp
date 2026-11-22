// Host-side self-test: runs the real MenuManager/UIButton/Settings code
// against a MOCK il2cpp backend (no Android, no libil2cpp needed).
//
//   g++ -std=c++20 -I src -I test/stubs -o /tmp/selftest
//       test/selftest.cpp src/Menu/MenuManager.cpp src/Menu/UIButton.cpp
//       src/Mods/Settings.cpp src/Crash/CrashLogger.cpp
//
// Verifies: build, hold-to-pin, poke toggles, highlight-only-when-on,
// drop-on-release physics, page switching, close, crash log export.
#include "../src/IL2CPP/GameAPI.h"
#include "../src/Crash/CrashLogger.h"
#include "../src/Menu/MenuManager.h"
#include "../src/Mods/Settings.h"
#include "../src/Logging.h"

#include <sys/wait.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ======================================================= mock il2cpp backend
namespace Game {

struct Node {
    Vector3 world{0, 0, 0};
    Vector3 local{0, 0, 0};
    Vector3 scale{1, 1, 1};
    int parent = 0;
    bool active = true;
    std::string text;
    Color color{1, 1, 1, 1};
    std::string name;
};
static std::map<int, Node> g_nodes;
static int g_next = 1;
static Node& N(int id) { return g_nodes[id]; }
static int NewNode(const char* name) { int id = g_next++; N(id).name = name; return id; }

bool Init() { return true; }
bool AttachCurrentThread() { return true; }
void DetachCurrentThread() {}

static bool g_gripL = false, g_gripR = false;
bool GripHeld(bool leftHand) { return leftHand ? g_gripL : g_gripR; }

static int g_leftHand = 0, g_rightHand = 0, g_head = 0;
static void InitHands() {
    g_leftHand = NewNode("LeftHand");
    g_rightHand = NewNode("RightHand");
    g_head = NewNode("Head");
    N(g_head).world = { 0, 1.6f, 0 };
}
Trans LeftHand()  { return (Trans)(intptr_t)g_leftHand; }
Trans RightHand() { return (Trans)(intptr_t)g_rightHand; }
Trans Head()      { return (Trans)(intptr_t)g_head; }

Vector3 Position(Trans t) { return N((int)(intptr_t)t).world; }
void SetPosition(Trans t, const Vector3& p) { N((int)(intptr_t)t).world = p; }
Vector3 LocalScale(Trans t) { return N((int)(intptr_t)t).scale; }
void SetLocalScale(Trans t, const Vector3& s) { N((int)(intptr_t)t).scale = s; }
void SetParent(Trans child, Trans parent) { N((int)(intptr_t)child).parent = (int)(intptr_t)parent; }
void Unparent(Trans child) { N((int)(intptr_t)child).parent = 0; }
bool IsActive(Obj go) { return N((int)(intptr_t)go).active; }
void SetActive(Obj go, bool on) { N((int)(intptr_t)go).active = on; }

Obj CreatePrimitive(int) { return (Obj)(intptr_t)NewNode("Cube"); }
Obj CreateEmptyObject()  { return (Obj)(intptr_t)NewNode("GO"); }
Trans TransformOf(Obj go) { return go; }
void SetRendererColor(Obj go, const Color& c) { N((int)(intptr_t)go).color = c; }
void AddTextMesh(Obj go, const char* text, int, const Color& c) {
    Node& n = N((int)(intptr_t)go);
    n.text = text;
    n.color = c;
}
void SetText(Obj tm, const char* text) { N((int)(intptr_t)tm).text = text; }
void SetTextColor(Obj tm, const Color& c) { N((int)(intptr_t)tm).color = c; }
void SetTextAnchorCenter(Obj) {}
void SetName(Obj, const char*) {}
void SetLayerRecursive(Obj, int) {}

Vector3 Forward(Trans) { return { 0, 0, 1 }; }
void SetLocalPosition(Trans t, const Vector3& p) {
    Node& n = N((int)(intptr_t)t);
    n.local = p;
    if (n.parent) {
        Node& pn = N(n.parent);
        n.world = { pn.world.x + p.x, pn.world.y + p.y, pn.world.z + p.z };
    } else {
        n.world = p;
    }
}
Vector3 WorldToLocal(Trans t, const Vector3& world) {
    Node& n = N((int)(intptr_t)t);
    return { world.x - n.world.x, world.y - n.world.y, world.z - n.world.z };
}

} // namespace Game

// ======================================================= test utilities
using namespace Game;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static int FindByText(const char* text) {
    for (auto& [id, n] : g_nodes) if (n.text == text) return id;
    return -1;
}

// Position the right hand so the fingertip lands on the button's center, then
// run one Update (fingertip = hand + forward*0.18, forward = +z in the mock).
static void Poke(const char* label) {
    int textId = FindByText(label);
    CHECK(textId > 0);
    if (textId <= 0) return;
    int btnId = g_nodes[textId].parent;
    CHECK(btnId > 0);
    Vector3 c = g_nodes[btnId].world;
    N(g_rightHand).world = { c.x, c.y, c.z - 0.18f };
    MenuManager::Update();
}

// Highlight quad = 2nd child of the button root (base cube, highlight, text).
static bool HighlightOn(const char* label) {
    int textId = FindByText(label);
    if (textId <= 0) return false;
    int btnId = g_nodes[textId].parent;
    std::vector<int> children;
    for (auto& [id, n] : g_nodes) if (id != btnId && n.parent == btnId) children.push_back(id);
    if (children.size() < 2) return false;
    return g_nodes[children[1]].active;
}

// ======================================================= tests
static void TestCrashLogger() {
    printf("- crash logger export\n");
    CrashLogger::SetFallbackDir("/tmp/monkeyclient-test");
    CrashLogger::Install();
    const std::string crashPath = "/tmp/monkeyclient-test/eclipse_mod_crash.txt";
    remove(crashPath.c_str());               // start clean

    // Two crashing children -> the append-mode file must hold TWO sections.
    for (int round = 1; round <= 2; ++round) {
        pid_t pid = fork();
        if (pid == 0) {
            MonkeyLog::Print("pre-crash milestone: about to deref null");
            volatile int* p = nullptr;
            *p = 1; // SIGSEGV -> handler appends a section, then re-raises
            _exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        CHECK(WIFSIGNALED(status));          // re-raise killed the child
    }

    std::ifstream crash(crashPath);
    std::stringstream buf;
    buf << crash.rdbuf();
    std::string content = buf.str();
    CHECK(!content.empty());
    int sections = 0;
    for (size_t pos = 0; (pos = content.find("==== " MENU_NAME " CRASH ====", pos))
                        != std::string::npos; pos += 4) ++sections;
    CHECK(sections == 2);                    // append across crashes
    CHECK(content.find("signal=11 (Segmentation fault)") != std::string::npos);
    CHECK(content.find("fault_addr=") != std::string::npos);
    CHECK(content.find("pc=0x") != std::string::npos);
    CHECK(content.find("x0=0x") != std::string::npos);
    CHECK(content.find("==== backtrace ====") != std::string::npos);
    CHECK(content.find("==== last log lines ====") != std::string::npos);
    CHECK(content.find("pre-crash milestone") != std::string::npos);
    CHECK(content.find("==== end ====") != std::string::npos);
    printf("  crash file OK: %s (%d sections)\n", crashPath.c_str(), sections);
}

int main() {
    printf("MonkeyClient self-test\n");
    Settings::Init();
    InitHands();
    CHECK(MenuManager::Build());

    // ---- 1. grab: menu pins in front of the hand ----
    g_gripR = true;
    N(g_rightHand).world = { 0.2f, 1.0f, 0.4f };
    MenuManager::Update();
    MenuManager::Update();
    int rootId = -1;
    for (auto& [id, n] : g_nodes)
        for (auto& [id2, n2] : g_nodes)
            if (id2 != id && n2.parent == id && n2.scale.x > 0.08f && n2.scale.x < 0.09f) rootId = id;
    CHECK(rootId > 0);
    CHECK(N(rootId).active);
    printf("- grab OK\n");

    // panel portrait check (height > width)
    int panelId = g_nodes[rootId].parent; // not used; find panel by scale
    int panel = -1;
    for (auto& [id, n] : g_nodes) if (n.parent == rootId && n.scale.x > 0.08f && n.scale.x < 0.09f) panel = id;
    CHECK(panel > 0);
    CHECK(N(panel).scale.y > N(panel).scale.z); // portrait: height > width
    (void)panelId;

    // ---- 2. switch to Settings tab ----
    Poke("Settings");
    CHECK(HighlightOn("Settings"));
    printf("- Settings tab active + highlighted\n");

    // ---- 3. Drop Menu toggle + release -> menu drops ----
    Poke("Drop Menu");
    CHECK(Settings::dropMenu == true);
    CHECK(HighlightOn("Drop Menu"));
    g_gripR = false;
    float y0 = N(rootId).world.y;
    for (int i = 0; i < 30; ++i) {
        usleep(2000); // real elapsed time so the dt-based physics moves
        MenuManager::Update();
    }
    float y1 = N(rootId).world.y;
    CHECK(N(rootId).active);  // dropped menu stays visible
    CHECK(y1 < y0);           // falling
    printf("- drop on release OK (y %.3f -> %.3f, still visible)\n", y0, y1);

    // ---- 4. re-grab the dropped menu, switch back to Main, close ----
    g_gripR = true;
    MenuManager::Update();
    CHECK(N(rootId).active);
    Poke("Main");
    CHECK(HighlightOn("Main"));
    Poke("Close Menu");
    MenuManager::Update(); // grip still held -> close lock must keep it hidden
    CHECK(!N(rootId).active);
    g_gripR = false;       // releasing clears the lock; next grip re-opens
    MenuManager::Update();
    g_gripR = true;
    MenuManager::Update();
    CHECK(N(rootId).active);
    printf("- re-grab, page switch, close + close-lock OK\n");

    // ---- 5. crash logger ----
    TestCrashLogger();

    printf("\n%s (%d failures)\n", g_failures ? "SELF-TEST FAILED" : "SELF-TEST PASSED", g_failures);
    return g_failures ? 1 : 0;
}
