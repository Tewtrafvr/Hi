#pragma once
// GameAPI — the ONLY place the template touches il2cpp / the game.
//
// Everything the menu needs is expressed as plain functions here, implemented
// through BNM (the same resolver library the original client links against —
// its symbols show BNM::Method<T>::Call / BNM::Field<T>::Get usage).
//
// VERIFY(BNM): BNM's Class::Find argument order and include layout differ
// slightly between BNM versions — adjust the marked lines against the BNM copy
// you drop into extern/BNM. All other files call only these functions.
#pragma once

#include <cstdint>

namespace Game {

using Obj    = void*;        // UnityEngine.Object*
using Trans  = void*;        // UnityEngine.Transform*

struct Vector3 { float x, y, z; };
struct Color  { float r, g, b, a; };

// ---------------- resolution ----------------
// Resolve all classes/methods once at startup. Returns false on failure
// (missing class/method names are logged, see main.cpp).
bool Init();

// il2cpp thread attach/detach — REQUIRED before calling any of the functions
// below from a non-main thread (see main.cpp's update thread).
bool AttachCurrentThread();
void DetachCurrentThread();

// ---------------- input ----------------
bool GripHeld(bool leftHand);                       // grip button state

// ---------------- player rig ----------------
Trans LeftHand();                                   // GorillaLocomotion Player left hand transform
Trans RightHand();
Trans Head();
Vector3 Position(Trans t);
void SetPosition(Trans t, const Vector3& p);
Vector3 LocalScale(Trans t);
void SetLocalScale(Trans t, const Vector3& s);
void SetParent(Trans child, Trans parent);          // world-position-stays
void Unparent(Trans child);
bool IsActive(Obj go);
void SetActive(Obj go, bool on);

// ---------------- world building ----------------
// primitiveType is UnityEngine.PrimitiveType: Sphere=0, Capsule=1, Cylinder=2,
// Cube=3, Plane=4, Quad=5.
inline constexpr int kPrimitiveCube = 3;
Obj CreatePrimitive(int primitiveType);
Obj CreateEmptyObject();                            // new GameObject()
Trans TransformOf(Obj go);
void  SetName(Obj go, const char* name);
// Opaque unlit-ish colored material on the primitive's renderer.
void  SetRendererColor(Obj go, const Color& c);
// TextMesh on a GameObject; font = built-in LegacyRuntime/Arial.
void  AddTextMesh(Obj go, const char* text, int charSize, const Color& c);
void  SetText(Obj tmObj, const char* text);
void  SetTextColor(Obj tmObj, const Color& c);
void  SetTextAnchorCenter(Obj tmObj);

// ---------------- space ----------------
Vector3 Forward(Trans t);                           // Transform.get_forward
void SetLocalPosition(Trans t, const Vector3& p);
Vector3 WorldToLocal(Trans t, const Vector3& world); // Transform.InverseTransformPoint

// ---------------- misc ----------------
// Layer the menu objects live on (Gorilla Tag ignores layers it doesn't
// collide with; 0 = Default is the usual choice for menus).
void SetLayerRecursive(Obj go, int layer);

} // namespace Game
