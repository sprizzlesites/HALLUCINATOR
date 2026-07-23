// A dependency-free VST3 module shim: this is the actual binary a DAW's
// LoadLibraryW() resolves at Contents/x86_64-win/HallucinatorRAVE.vst3 (see
// CMakeLists.txt's WIN32 POST_BUILD step, which renames the real plugin
// binary to HallucinatorRAVE_Impl.dll and installs this shim in its place).
// It has zero torch/JUCE dependencies, so its own load always succeeds
// regardless of the host's search path.
//
// The real implementation statically imports torch_cpu.dll and c10.dll,
// which live next to it but aren't found by a normal LoadLibraryW() from an
// arbitrary host process - Windows only searches the *host process's* own
// directory and PATH for a dependent DLL, never "the directory the
// requesting DLL itself lives in" (confirmed via a real FL Studio bug
// report, then empirically via CI's isolated-directory smoke test). A first
// attempt at fixing this with /DELAYLOAD (see git history) hit a hard MSVC
// limitation: c10.dll exports a data symbol, and delay-loading can only
// defer function calls, never raw data-symbol access (LNK1194).
//
// This shim sidesteps both problems at once: it explicitly
// LoadLibraryExW()s the impl DLL by absolute path with
// LOAD_WITH_ALTERED_SEARCH_PATH, which makes the loader search *that*
// directory for every one of its imports - function or data - as part of
// ordinary PE loading, not the more limited delay-load mechanism.
#include <windows.h>
#include <string>

namespace
{
    // Address of this function only serves to tell GetModuleHandleExW which
    // module (DLL) this translation unit ended up compiled into - there's
    // no other portable way to ask "what module am I" than pointing at code
    // known to live inside it.
    void thisModuleAnchor() {}

    std::wstring getOwnModuleDirectory()
    {
        HMODULE ownModule = nullptr;
        if (! GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                     | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<LPCWSTR>(&thisModuleAnchor),
                                 &ownModule))
            return {};

        wchar_t path[MAX_PATH];
        DWORD len = GetModuleFileNameW(ownModule, path, MAX_PATH);
        if (len == 0 || len == MAX_PATH)
            return {};

        std::wstring full(path, len);
        auto lastSlash = full.find_last_of(L"\\/");
        return lastSlash == std::wstring::npos ? std::wstring {} : full.substr(0, lastSlash);
    }

    // Set only inside getImplModule()'s one-time load attempt below, so
    // HallucinatorShimDiagnostics() (see bottom of file) can report exactly
    // what path was tried and why it failed - GetPluginFactory() alone
    // only ever returns null on failure, silently swallowing GetLastError()
    // and the computed path, which left a real user-reported load failure
    // undiagnosable from the outside (see tools/vst3_load_diagnostic.cpp).
    std::wstring lastImplPathAttempted;
    DWORD lastImplLoadError = 0;

    // Lazy, thread-safe (C++11 static-init) load: deferred until the host
    // first actually calls into the plugin, well after this shim's own
    // DllMain has returned - loading another DLL from inside DllMain risks
    // deadlocking on the loader lock.
    HMODULE getImplModule()
    {
        static HMODULE implModule = [] () -> HMODULE
        {
            auto dir = getOwnModuleDirectory();
            if (dir.empty())
            {
                lastImplLoadError = ERROR_PATH_NOT_FOUND; // couldn't even determine our own directory
                return nullptr;
            }

            lastImplPathAttempted = dir + L"\\HallucinatorRAVE_Impl.dll";
            HMODULE h = LoadLibraryExW(lastImplPathAttempted.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (h == nullptr)
                lastImplLoadError = GetLastError();
            return h;
        }();

        return implModule;
    }

    template <typename Fn>
    Fn getImplProc(const char* name)
    {
        auto m = getImplModule();
        return m == nullptr ? nullptr : reinterpret_cast<Fn>(GetProcAddress(m, name));
    }
}

// Signatures match the VST3 SDK's module entry points exactly (see
// pluginterfaces/base/ipluginbase.h's GetFactoryProc and public.sdk's
// InitModuleFunc/ExitModuleFunc) modulo the PLUGIN_API (__stdcall) calling
// convention keyword, which is a no-op on x64 - this project only builds
// x64 Windows plugins, so omitting it changes nothing.
extern "C" __declspec(dllexport) void* GetPluginFactory()
{
    using FnType = void* (*) ();
    auto fn = getImplProc<FnType>("GetPluginFactory");
    return fn != nullptr ? fn() : nullptr;
}

extern "C" __declspec(dllexport) bool InitDll()
{
    using FnType = bool (*) ();
    auto fn = getImplProc<FnType>("InitDll");
    return fn != nullptr ? fn() : true;
}

extern "C" __declspec(dllexport) bool ExitDll()
{
    using FnType = bool (*) ();
    auto fn = getImplProc<FnType>("ExitDll");
    return fn != nullptr ? fn() : true;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}

// Diagnostic-only export (see tools/vst3_load_diagnostic.cpp): forces the
// lazy impl load attempt (if not already made) and reports exactly what
// path was tried, whether it succeeded, and the Win32 error code if not -
// none of which GetPluginFactory() alone exposes, since it just returns
// null either way. pathBufOut/pathBufLen follow the usual "buffer + max
// length including the NUL" convention; either output pointer may be null
// if the caller doesn't need it.
extern "C" __declspec(dllexport) void HallucinatorShimDiagnostics(wchar_t* pathBufOut, int pathBufLen,
                                                                    unsigned long* lastErrorOut, int* loadedOut)
{
    HMODULE m = getImplModule();

    if (loadedOut != nullptr)
        *loadedOut = (m != nullptr) ? 1 : 0;

    if (lastErrorOut != nullptr)
        *lastErrorOut = lastImplLoadError;

    if (pathBufOut != nullptr && pathBufLen > 0)
    {
        size_t n = lastImplPathAttempted.copy(pathBufOut, (size_t) (pathBufLen - 1));
        pathBufOut[n] = L'\0';
    }
}
