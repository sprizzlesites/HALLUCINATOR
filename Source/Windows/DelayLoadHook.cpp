#if defined(_WIN32)

#include <windows.h>
#include <delayimp.h>
#include <string>

namespace
{
    // Address of this function only serves to tell GetModuleHandleExW which
    // module (DLL) this translation unit ended up compiled into - there's no
    // other portable way to ask "what module am I" than pointing at code
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

    std::wstring widenUtf8(const char* s)
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        if (len <= 0)
            return {};
        std::wstring out(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
        out.resize(static_cast<size_t>(len) - 1); // drop the NUL terminator len counted
        return out;
    }

    // Windows' standard DLL search order for a dependent DLL (e.g.
    // torch_cpu.dll, imported by this plugin's own code) only checks the
    // *hosting process's* own directory (e.g. FL Studio's .exe folder) and
    // PATH - never "the directory the requesting DLL itself lives in" -
    // unless the loader is told otherwise. CMakeLists.txt's Windows
    // POST_BUILD step copies torch_cpu.dll/c10.dll (and their own further
    // dependencies) next to the built plugin binary, but that placement is
    // silently useless for any host process that doesn't happen to look
    // there, which in practice is every real DAW. torch_cpu.dll and c10.dll
    // are declared /DELAYLOAD (see CMakeLists.txt) specifically so this
    // hook - not the OS loader's default search - resolves them: loading by
    // absolute path with LOAD_WITH_ALTERED_SEARCH_PATH, which also makes the
    // loader search that same directory for *their* dependencies in turn
    // (e.g. c10.dll's own imports, or whatever else ships in torch's lib/
    // folder), so nothing beyond these two needs its own /DELAYLOAD entry.
    FARPROC WINAPI delayLoadNotifyHook(unsigned dliNotify, PDelayLoadInfo pdli)
    {
        if (dliNotify != dliNotePreLoadLibrary)
            return nullptr;

        static const std::wstring ownDir = getOwnModuleDirectory();
        if (ownDir.empty())
            return nullptr;

        auto dllName = widenUtf8(pdli->szDll);
        if (dllName.empty())
            return nullptr;

        std::wstring fullPath = ownDir + L"\\" + dllName;
        HMODULE h = LoadLibraryExW(fullPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        return reinterpret_cast<FARPROC>(h);
    }
}

extern "C" PfnDliHook __pfnDliNotifyHook2 = delayLoadNotifyHook;

#endif // _WIN32
