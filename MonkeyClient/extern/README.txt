This template has NO external dependencies: the il2cpp access layer
(src/IL2CPP/GameAPI.cpp) talks to libil2cpp.so directly through its public C
exports (il2cpp_class_get_method_from_name + il2cpp_runtime_invoke).

If you want native function hooking for extra mods (like the original client
does with Dobby), drop the Dobby source into extern/Dobby and add it to
CMakeLists.txt yourself — not required for the menu.
