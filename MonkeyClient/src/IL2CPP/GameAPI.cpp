// GameAPI implementation — every il2cpp access lives here.
//
// Strategy: the game process already has libil2cpp.so loaded; we dlsym the
// PUBLIC, version-stable il2cpp C exports (class/method lookup +
// il2cpp_runtime_invoke) and talk to the game through them. No BNM/Dobby
// dependency needed to build this template.
#include "GameAPI.h"

#include <dlfcn.h>
#include <cstring>
#include <cstdint>
#include <string>

#include "../Config.h"
#include "../Logging.h"

using namespace Game;

// ---------------------------------------------------------------- il2cpp API
extern "C" {
typedef struct Il2CppDomain Il2CppDomain;
typedef struct Il2CppAssembly Il2CppAssembly;
typedef struct Il2CppImage Il2CppImage;
typedef struct Il2CppClass Il2CppClass;
typedef struct MethodInfo MethodInfo;
typedef struct FieldInfo FieldInfo;
typedef struct Il2CppAssemblyName Il2CppAssemblyName;
typedef struct Il2CppObject Il2CppObject;
typedef Il2CppObject Il2CppReflectionType;
typedef Il2CppObject Il2CppString;
typedef Il2CppObject Il2CppException;
typedef struct Il2CppThread Il2CppThread;
}

namespace {
struct Il2CppApi {
    Il2CppDomain* (*domain_get)();
    const Il2CppAssembly** (*domain_get_assemblies)(Il2CppDomain*, size_t*);
    Il2CppImage* (*assembly_get_image)(const Il2CppAssembly*);
    Il2CppClass* (*class_from_name)(const Il2CppImage*, const char* ns, const char* name);
    MethodInfo* (*class_get_method_from_name)(Il2CppClass*, const char*, int);
    FieldInfo* (*class_get_field_from_name)(Il2CppClass*, const char*);
    Il2CppReflectionType* (*class_get_type)(Il2CppClass*);
    Il2CppObject* (*runtime_invoke)(const MethodInfo*, void* obj, void** params, Il2CppException** exc);
    void* (*object_unbox)(Il2CppObject*);
    Il2CppString* (*string_new)(const char*);
    void (*field_get_value)(Il2CppObject*, FieldInfo*, void*);
    Il2CppObject* (*object_new)(Il2CppClass*);
    Il2CppThread* (*thread_attach)(Il2CppDomain*);
    int (*thread_detach)(Il2CppThread*);
} api{};

bool LoadIl2Cpp() {
    void* h = dlopen("libil2cpp.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) h = dlopen("libil2cpp.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { MonkeyLog::Print("GameAPI: libil2cpp.so not loadable: %s", dlerror()); return false; }
    auto sym = [&](const char* n) { return dlsym(h, n); };
    api.domain_get            = (decltype(api.domain_get))sym("il2cpp_domain_get");
    api.domain_get_assemblies = (decltype(api.domain_get_assemblies))sym("il2cpp_domain_get_assemblies");
    api.assembly_get_image    = (decltype(api.assembly_get_image))sym("il2cpp_assembly_get_image");
    api.class_from_name       = (decltype(api.class_from_name))sym("il2cpp_class_from_name");
    api.class_get_method_from_name = (decltype(api.class_get_method_from_name))sym("il2cpp_class_get_method_from_name");
    api.class_get_field_from_name  = (decltype(api.class_get_field_from_name))sym("il2cpp_class_get_field_from_name");
    api.class_get_type        = (decltype(api.class_get_type))sym("il2cpp_class_get_type");
    api.runtime_invoke        = (decltype(api.runtime_invoke))sym("il2cpp_runtime_invoke");
    api.object_unbox          = (decltype(api.object_unbox))sym("il2cpp_object_unbox");
    api.string_new            = (decltype(api.string_new))sym("il2cpp_string_new");
    api.field_get_value       = (decltype(api.field_get_value))sym("il2cpp_field_get_value");
    api.object_new            = (decltype(api.object_new))sym("il2cpp_object_new");
    api.thread_attach         = (decltype(api.thread_attach))sym("il2cpp_thread_attach");
    api.thread_detach         = (decltype(api.thread_detach))sym("il2cpp_thread_detach");
    return api.domain_get && api.domain_get_assemblies && api.assembly_get_image &&
           api.class_from_name && api.class_get_method_from_name &&
           api.class_get_field_from_name && api.class_get_type &&
           api.runtime_invoke && api.object_unbox && api.string_new &&
           api.field_get_value && api.object_new;
}

// Method handle wrapper: binds + invokes.
struct M {
    MethodInfo* m = nullptr;
    bool ok = false;
    void Bind(Il2CppClass* k, const char* name, int argc) {
        if (k) m = api.class_get_method_from_name(k, name, argc);
        ok = m != nullptr;
        if (!ok) MonkeyLog::Print("GameAPI: method missing: %s (%d args)", name, argc);
    }
    // Invoke with unboxed params. Value results come back boxed -> unbox.
    void* Invoke(void* instance, void** params) {
        Il2CppException* exc = nullptr;
        void* r = api.runtime_invoke(m, instance, params, &exc);
        return (exc || !r) ? nullptr : r;
    }
};

struct K { Il2CppClass* k = nullptr; bool ok = false; };

// ------------------------------------------------------------ helpers
template <typename T>
T InvokeVal(const M& m, void* inst, void** params) {
    Il2CppException* exc = nullptr;
    void* r = api.runtime_invoke(m.m, inst, params, &exc);
    if (!r || exc) return T{};
    void* unboxed = api.object_unbox((Il2CppObject*)r);
    return unboxed ? *(T*)unboxed : T{};
}

} // namespace

namespace Game {

// ---------------------------------------------------------- resolved state
static Obj GameObjectOf(Trans t); // fwd decl (defined below, used by SetLayerRecursive)
static bool g_ready = false;
static K kGameObject, kTransform, kInput, kTextMesh, kRenderer, kMaterial,
         kFont, kResources, kPlayer, kType, kComponent;

// GameObject methods
static M mCreatePrimitive, mGOgetTransform, mGOSetActive, mGOgetActiveSelf,
         mGOsetLayer, mGOGetComponent, mGOAddComponent;
// Transform methods
static M mTgetPosition, mTsetPosition, mTgetLocalScale, mTsetLocalScale,
         mTSetParent, mTgetChildCount, mTGetChild, mTgetGameObject,
         mTgetForward, mTsetLocalPosition, mTInverseTransformPoint;
// Input / player
static M mInputGetKey, mPlayerGetInstance;
// TextMesh
static M mTMsetText, mTMsetColor, mTMsetCharSize, mTMsetFontSize, mTMsetAnchor,
         mTMsetFont;
// Renderer / Material / Resources
static M mRgetMaterial, mMsetColor, mResLoad;

// Player rig fields
static FieldInfo* fLeftHand = nullptr;
static FieldInfo* fRightHand = nullptr;
static FieldInfo* fHead = nullptr;
static Obj g_player = nullptr;

static Il2CppString* NewStr(const char* s) { return api.string_new(s); }
static void* TypeOf(const K& k) { return k.k ? api.class_get_type(k.k) : nullptr; }

static Il2CppClass* FindClassAcrossImages(const char* ns, const char* name) {
    Il2CppDomain* dom = api.domain_get();
    if (!dom) return nullptr;
    size_t n = 0;
    const Il2CppAssembly** asms = api.domain_get_assemblies(dom, &n);
    for (size_t i = 0; i < n; ++i) {
        Il2CppImage* img = api.assembly_get_image(asms[i]);
        if (!img) continue;
        if (Il2CppClass* k = api.class_from_name(img, ns, name)) return k;
    }
    return nullptr;
}

static void Bind(K& k, const char* ns, const char* name) {
    k.k = FindClassAcrossImages(ns, name);
    k.ok = k.k != nullptr;
    if (!k.ok) MonkeyLog::Print("GameAPI: class missing: %s.%s", ns, name);
}

// ---------------------------------------------------------- Init
bool Init() {
    if (!LoadIl2Cpp()) return false;

    Bind(kGameObject, "UnityEngine", "GameObject");
    Bind(kTransform,  "UnityEngine", "Transform");
    Bind(kInput,      "UnityEngine", "Input");
    Bind(kTextMesh,   "UnityEngine", "TextMesh");
    Bind(kRenderer,   "UnityEngine", "Renderer");
    Bind(kMaterial,   "UnityEngine", "Material");
    Bind(kFont,       "UnityEngine", "Font");
    Bind(kResources,  "UnityEngine", "Resources");
    Bind(kType,       "System",      "Type");
    Bind(kComponent,  "UnityEngine", "Component");
    Bind(kPlayer,     "GorillaLocomotion", "Player");

    if (!kGameObject.ok || !kTransform.ok || !kInput.ok || !kPlayer.ok ||
        !kTextMesh.ok || !kRenderer.ok || !kMaterial.ok || !kResources.ok ||
        !kType.ok || !kComponent.ok)
        return false;

    mCreatePrimitive.Bind(kGameObject.k, "CreatePrimitive", 1);
    mGOgetTransform.Bind(kGameObject.k, "get_transform", 0);
    mGOSetActive.Bind(kGameObject.k, "SetActive", 1);
    mGOgetActiveSelf.Bind(kGameObject.k, "get_activeSelf", 0);
    mGOsetLayer.Bind(kGameObject.k, "set_layer", 1);
    mGOGetComponent.Bind(kGameObject.k, "GetComponent", 1);
    mGOAddComponent.Bind(kGameObject.k, "AddComponent", 1);
    mTgetPosition.Bind(kTransform.k, "get_position", 0);
    mTsetPosition.Bind(kTransform.k, "set_position", 1);
    mTgetLocalScale.Bind(kTransform.k, "get_localScale", 0);
    mTsetLocalScale.Bind(kTransform.k, "set_localScale", 1);
    mTSetParent.Bind(kTransform.k, "SetParent", 2);
    mTgetChildCount.Bind(kTransform.k, "get_childCount", 0);
    mTGetChild.Bind(kTransform.k, "GetChild", 1);
    mTgetGameObject.Bind(kTransform.k, "get_gameObject", 0);
    mTgetForward.Bind(kTransform.k, "get_forward", 0);
    mTsetLocalPosition.Bind(kTransform.k, "set_localPosition", 1);
    mTInverseTransformPoint.Bind(kTransform.k, "InverseTransformPoint", 1);
    mInputGetKey.Bind(kInput.k, "GetKey", 1);
    mPlayerGetInstance.Bind(kPlayer.k, "get_Instance", 0);
    mTMsetText.Bind(kTextMesh.k, "set_text", 1);
    mTMsetColor.Bind(kTextMesh.k, "set_color", 1);
    mTMsetCharSize.Bind(kTextMesh.k, "set_characterSize", 1);
    mTMsetFontSize.Bind(kTextMesh.k, "set_fontSize", 1);
    mTMsetAnchor.Bind(kTextMesh.k, "set_anchor", 1);
    mTMsetFont.Bind(kTextMesh.k, "set_font", 1);
    mRgetMaterial.Bind(kRenderer.k, "get_material", 0);
    mMsetColor.Bind(kMaterial.k, "set_color", 1);
    mResLoad.Bind(kResources.k, "Load", 1);

    fLeftHand  = api.class_get_field_from_name(kPlayer.k, "leftHandTransform");
    fRightHand = api.class_get_field_from_name(kPlayer.k, "rightHandTransform");
    fHead      = api.class_get_field_from_name(kPlayer.k, "headTransform");
    if (!fLeftHand || !fRightHand || !fHead) {
        MonkeyLog::Print("GameAPI: Player rig fields not found — check your dump "
                         "(leftHandTransform/rightHandTransform/headTransform)");
        return false;
    }

    g_ready = true;
    MonkeyLog::Print("GameAPI init OK");
    return true;
}

// ---------------------------------------------------------- thread attach
bool AttachCurrentThread() {
    if (!api.thread_attach) return false;
    Il2CppDomain* dom = api.domain_get();
    return dom && api.thread_attach(dom) != nullptr;
}
void DetachCurrentThread() {
    // The current thread pointer isn't exported portably; detaching is optional
    // for a mod that lives as long as the process. No-op by default.
}

// ---------------------------------------------------------- input
bool GripHeld(bool leftHand) {
    if (!g_ready || !mInputGetKey.ok) return false;
    int key = leftHand ? kKeyLeftGrip : kKeyRightGrip;
    void* params[] = { &key };
    return InvokeVal<bool>(mInputGetKey, nullptr, params) != 0;
}

// ---------------------------------------------------------- player rig
static Obj PlayerInstance() {
    if (!g_player && mPlayerGetInstance.ok)
        g_player = (Obj)mPlayerGetInstance.Invoke(nullptr, nullptr);
    return g_player;
}

static Trans RigField(FieldInfo* f) {
    Obj p = PlayerInstance();
    if (!p || !f) return nullptr;
    Trans out = nullptr;
    api.field_get_value((Il2CppObject*)p, f, &out);
    return out;
}

Trans LeftHand()  { return RigField(fLeftHand);  }
Trans RightHand() { return RigField(fRightHand); }
Trans Head()      { return RigField(fHead); }

// ---------------------------------------------------------- transform
Vector3 Position(Trans t) {
    if (!t || !mTgetPosition.ok) return {0, 0, 0};
    return InvokeVal<Vector3>(mTgetPosition, t, nullptr);
}
void SetPosition(Trans t, const Vector3& p) {
    if (!t || !mTsetPosition.ok) return;
    void* params[] = { (void*)&p };
    mTsetPosition.Invoke(t, params);
}
Vector3 LocalScale(Trans t) {
    if (!t || !mTgetLocalScale.ok) return {1, 1, 1};
    return InvokeVal<Vector3>(mTgetLocalScale, t, nullptr);
}
void SetLocalScale(Trans t, const Vector3& s) {
    if (!t || !mTsetLocalScale.ok) return;
    void* params[] = { (void*)&s };
    mTsetLocalScale.Invoke(t, params);
}
void SetParent(Trans child, Trans parent) {
    if (!child || !mTSetParent.ok) return;
    uint8_t stay = 1; // worldPositionStays = true
    void* params[] = { &parent, &stay };
    mTSetParent.Invoke(child, params);
}
void Unparent(Trans child) {
    if (!child || !mTSetParent.ok) return;
    uint8_t stay = 1;
    void* parent = nullptr;
    void* params[] = { &parent, &stay };
    mTSetParent.Invoke(child, params);
}
bool IsActive(Obj go) {
    if (!go || !mGOgetActiveSelf.ok) return false;
    return InvokeVal<bool>(mGOgetActiveSelf, go, nullptr) != 0;
}
void SetActive(Obj go, bool on) {
    if (!go || !mGOSetActive.ok) return;
    uint8_t v = on ? 1 : 0;
    void* params[] = { &v };
    mGOSetActive.Invoke(go, params);
}
void SetLayerRecursive(Obj go, int layer) {
    if (!go || !mGOsetLayer.ok || !mGOgetTransform.ok || !mTgetChildCount.ok || !mTGetChild.ok) return;
    // depth-first over the (shallow) menu hierarchy
    struct Rec {
        static void Walk(Trans t, int layer) {
            void* pl[] = { &layer };
            mGOsetLayer.Invoke((Obj)GameObjectOf(t), pl);
            int count = InvokeVal<int>(mTgetChildCount, t, nullptr);
            for (int i = 0; i < count; ++i) {
                void* pi[] = { &i };
                Trans child = (Trans)mTGetChild.Invoke(t, pi);
                if (child) Walk(child, layer);
            }
        }
    };
    Rec::Walk(TransformOf(go), layer);
}

// ---------------------------------------------------------- world building
Obj CreatePrimitive(int primitiveType) {
    if (!mCreatePrimitive.ok) return nullptr;
    void* params[] = { &primitiveType };
    return (Obj)mCreatePrimitive.Invoke(nullptr, params);
}

Obj CreateEmptyObject() {
    if (!kGameObject.k || !api.object_new) return nullptr;
    Il2CppObject* go = api.object_new(kGameObject.k);
    if (!go) return nullptr;
    // Run the GameObject() constructor on the fresh object.
    MethodInfo* ctor = api.class_get_method_from_name(kGameObject.k, ".ctor", 0);
    if (ctor) api.runtime_invoke(ctor, go, nullptr, nullptr);
    return (Obj)go;
}
Trans TransformOf(Obj go) {
    if (!go || !mGOgetTransform.ok) return nullptr;
    return (Trans)mGOgetTransform.Invoke(go, nullptr);
}
static Obj GameObjectOf(Trans t) {
    if (!t || !mTgetGameObject.ok) return nullptr;
    return (Obj)mTgetGameObject.Invoke(t, nullptr);
}
void SetName(Obj go, const char* name) { /* optional, unused by menu */ (void)go; (void)name; }

// GetComponent(Type) / AddComponent(Type) via System.Type objects.
static Obj ComponentOf(Obj go, const K& typeClass) {
    if (!go || !mGOGetComponent.ok || !typeClass.k) return nullptr;
    void* type = TypeOf(typeClass);
    void* params[] = { &type };
    return (Obj)mGOGetComponent.Invoke(go, params);
}

void SetRendererColor(Obj go, const Color& c) {
    if (!go) return;
    Obj renderer = ComponentOf(go, kRenderer);
    if (!renderer || !mRgetMaterial.ok) return;
    Obj mat = (Obj)mRgetMaterial.Invoke(renderer, nullptr);
    if (!mat || !mMsetColor.ok) return;
    void* params[] = { (void*)&c };
    mMsetColor.Invoke(mat, params);
}

void AddTextMesh(Obj go, const char* text, int charSize, const Color& c) {
    if (!go || !mGOAddComponent.ok) return;
    void* type = TypeOf(kTextMesh);
    void* params[] = { &type };
    Obj tm = (Obj)mGOAddComponent.Invoke(go, params);
    if (!tm) return;
    SetTextAnchorCenter(tm);
    if (mTMsetCharSize.ok) { void* p[] = { &charSize }; mTMsetCharSize.Invoke(tm, p); }
    int fontSize = 64;
    if (mTMsetFontSize.ok) { void* p[] = { &fontSize }; mTMsetFontSize.Invoke(tm, p); }
    SetTextColor(tm, c);
    SetText(tm, text);
    // Built-in font so the text renders: Resources.Load<Font>("LegacyRuntime.ttf")
    // with Arial.ttf fallback (older Unity versions).
    if (mResLoad.ok && kFont.k && mTMsetFont.ok) {
        const char* names[] = { "LegacyRuntime.ttf", "Arial.ttf" };
        for (const char* n : names) {
            Il2CppString* s = NewStr(n);
            void* p[] = { &s };
            Obj font = (Obj)mResLoad.Invoke(nullptr, p);
            if (font) { void* pf[] = { &font }; mTMsetFont.Invoke(tm, pf); break; }
        }
    }
}

void SetText(Obj tmObj, const char* text) {
    if (!tmObj || !mTMsetText.ok) return;
    Il2CppString* s = NewStr(text);
    void* params[] = { &s };
    mTMsetText.Invoke(tmObj, params);
}
void SetTextColor(Obj tmObj, const Color& c) {
    if (!tmObj || !mTMsetColor.ok) return;
    void* params[] = { (void*)&c };
    mTMsetColor.Invoke(tmObj, params);
}
void SetTextAnchorCenter(Obj tmObj) {
    if (!tmObj || !mTMsetAnchor.ok) return;
    int anchor = 4; // TextAnchor.MiddleCenter
    void* params[] = { &anchor };
    mTMsetAnchor.Invoke(tmObj, params);
}

// ---------------------------------------------------------- space
Vector3 Forward(Trans t) {
    if (!t || !mTgetForward.ok) return { 0, 1, 0 };
    return InvokeVal<Vector3>(mTgetForward, t, nullptr);
}

void SetLocalPosition(Trans t, const Vector3& p) {
    if (!t || !mTsetLocalPosition.ok) return;
    void* params[] = { (void*)&p };
    mTsetLocalPosition.Invoke(t, params);
}

Vector3 WorldToLocal(Trans t, const Vector3& world) {
    if (!t || !mTInverseTransformPoint.ok) return { 0, 0, 0 };
    void* params[] = { (void*)&world };
    return InvokeVal<Vector3>(mTInverseTransformPoint, t, params);
}

} // namespace Game
