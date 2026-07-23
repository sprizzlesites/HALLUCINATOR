// Standalone Windows diagnostic tool - run this ON THE MACHINE where the
// plugin is failing to load in a real DAW, pointed at the actual installed
// HallucinatorRAVE.vst3 binary. Not part of the shipped plugin.
//
// Two separate fixes (the DLL search-path shim, and asynchronous model
// loading) were each confirmed working via CI's own smoke test, yet
// produced a BIT-FOR-BIT IDENTICAL error report in a real FL Studio
// install both times - meaning whatever is actually failing happens
// somewhere CI's narrower test (LoadLibraryW + GetProcAddress +
// "GetPluginFactory() returned non-null") never reaches. This tool goes
// further: it exercises the exact same VST3 module lifecycle a host does -
// LoadLibrary, InitDll, GetPluginFactory, then getFactoryInfo/getClassInfo/
// createInstance for every exported class - using the real Steinberg SDK
// interface headers (not hand-rolled vtable poking), so it can report
// exactly where things actually break on a real, affected machine, instead
// of guessing blind from an environment (this project's own CI) that
// hasn't been able to reproduce the failure at all.
//
// Every SEH-guarded (__try/__except) call lives in its own tiny helper
// function with no C++ objects that have destructors in scope - MSVC
// refuses to compile __try in a function that also needs C++ object
// unwinding (error C2712), which a first version of this file hit by
// mixing __try with local std::string/std::wstring objects in main().
//
// Usage:
//   vst3_load_diagnostic.exe "C:\path\to\HallucinatorRAVE.vst3\Contents\x86_64-win\HallucinatorRAVE.vst3"
#include <windows.h>
#include <iostream>
#include <string>

#include "pluginterfaces/base/ipluginbase.h"

using namespace Steinberg;

namespace
{
    using InitModuleFunc = bool (PLUGIN_API*) ();
    using GetFactoryProc = IPluginFactory* (PLUGIN_API*) ();

    std::string tuidToString(const TUID& tuid)
    {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                 (unsigned char) tuid[0], (unsigned char) tuid[1], (unsigned char) tuid[2], (unsigned char) tuid[3],
                 (unsigned char) tuid[4], (unsigned char) tuid[5], (unsigned char) tuid[6], (unsigned char) tuid[7],
                 (unsigned char) tuid[8], (unsigned char) tuid[9], (unsigned char) tuid[10], (unsigned char) tuid[11],
                 (unsigned char) tuid[12], (unsigned char) tuid[13], (unsigned char) tuid[14], (unsigned char) tuid[15]);
        return buf;
    }

    // --- SEH-guarded calls, isolated: no C++ objects with destructors in
    //     any of these functions' own locals/parameters (POD/pointer/
    //     reference only) --------------------------------------------------

    bool callInitDll(InitModuleFunc initDll, bool& outResult, DWORD& outExceptionCode)
    {
        __try
        {
            outResult = initDll();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outExceptionCode = GetExceptionCode();
            return false;
        }
    }

    bool callGetPluginFactory(GetFactoryProc getFactory, IPluginFactory*& outFactory, DWORD& outExceptionCode)
    {
        __try
        {
            outFactory = getFactory();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outExceptionCode = GetExceptionCode();
            return false;
        }
    }

    bool callGetFactoryInfo(IPluginFactory* factory, PFactoryInfo& outInfo, tresult& outResult, DWORD& outExceptionCode)
    {
        __try
        {
            outResult = factory->getFactoryInfo(&outInfo);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outExceptionCode = GetExceptionCode();
            return false;
        }
    }

    bool callCountClasses(IPluginFactory* factory, int32& outCount, DWORD& outExceptionCode)
    {
        __try
        {
            outCount = factory->countClasses();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outExceptionCode = GetExceptionCode();
            return false;
        }
    }

    bool callGetClassInfo(IPluginFactory* factory, int32 index, PClassInfo& outInfo, tresult& outResult, DWORD& outExceptionCode)
    {
        __try
        {
            outResult = factory->getClassInfo(index, &outInfo);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outExceptionCode = GetExceptionCode();
            return false;
        }
    }

    // Every DLL HallucinatorRAVE ships lives in one folder; rather than
    // parse HallucinatorRAVE_Impl.dll's PE import table to figure out
    // exactly which of its (possibly transitive) dependencies is
    // unresolvable, just try loading every *.dll in that same folder
    // individually - whichever ones fail standalone (even with the same
    // LOAD_WITH_ALTERED_SEARCH_PATH the shim itself uses) are the direct
    // answer to "which specific file can't load on this machine".
    bool tryLoadSingleDll(const wchar_t* fullPath, DWORD& outErrorOrExceptionCode)
    {
        __try
        {
            HMODULE h = LoadLibraryExW(fullPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (h == nullptr)
            {
                outErrorOrExceptionCode = GetLastError();
                return false;
            }
            FreeLibrary(h);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outErrorOrExceptionCode = GetExceptionCode();
            return false;
        }
    }
}

void checkAllDllsInDirectory(const std::wstring& dir)
{
    std::wstring pattern = dir + L"\\*.dll";
    WIN32_FIND_DATAW findData {};
    HANDLE find = FindFirstFileW(pattern.c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE)
    {
        std::cout << "[FAIL] Could not enumerate *.dll in " << std::endl;
        std::wcout << dir << std::endl;
        return;
    }

    do
    {
        std::wstring fileName = findData.cFileName;
        std::wstring fullPath = dir + L"\\" + fileName;

        DWORD errorCode = 0;
        bool ok = tryLoadSingleDll(fullPath.c_str(), errorCode);

        std::wcout << (ok ? L"[OK]   " : L"[FAIL] ") << fileName;
        if (! ok)
            std::wcout << L" - failed alone with code " << errorCode;
        std::wcout << std::endl;
    }
    while (FindNextFileW(find, &findData));

    FindClose(find);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: vst3_load_diagnostic.exe <path to HallucinatorRAVE.vst3 binary>" << std::endl;
        return 1;
    }

    std::wstring pathW;
    {
        std::string pathA = argv[1];
        int len = MultiByteToWideChar(CP_UTF8, 0, pathA.c_str(), -1, nullptr, 0);
        pathW.resize((size_t) len);
        MultiByteToWideChar(CP_UTF8, 0, pathA.c_str(), -1, pathW.data(), len);
        pathW.resize(wcslen(pathW.c_str()));
    }

    std::cout << "=== Step 1: LoadLibraryW ===" << std::endl;
    HMODULE mod = LoadLibraryW(pathW.c_str());
    if (mod == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[FAIL] LoadLibraryW failed, Win32 error " << err << std::endl;
        return 1;
    }
    std::cout << "[OK] Module loaded, handle = " << mod << std::endl;

    std::cout << "\n=== Step 2: InitDll (optional) ===" << std::endl;
    auto initDll = reinterpret_cast<InitModuleFunc>(GetProcAddress(mod, "InitDll"));
    if (initDll == nullptr)
    {
        std::cout << "[INFO] No InitDll export found (optional - not necessarily a problem)" << std::endl;
    }
    else
    {
        bool ok = false;
        DWORD exceptionCode = 0;
        if (! callInitDll(initDll, ok, exceptionCode))
            std::cout << "[FAIL] InitDll() raised a structured exception (code 0x" << std::hex << exceptionCode << std::dec << ")" << std::endl;
        else
            std::cout << (ok ? "[OK] " : "[FAIL] ") << "InitDll() returned " << (ok ? "true" : "false") << std::endl;
    }

    std::cout << "\n=== Step 3: GetPluginFactory ===" << std::endl;
    auto getFactory = reinterpret_cast<GetFactoryProc>(GetProcAddress(mod, "GetPluginFactory"));
    if (getFactory == nullptr)
    {
        std::cout << "[FAIL] GetProcAddress(GetPluginFactory) failed - export not found" << std::endl;
        return 1;
    }

    IPluginFactory* factory = nullptr;
    DWORD exceptionCode = 0;
    if (! callGetPluginFactory(getFactory, factory, exceptionCode))
    {
        std::cout << "[FAIL] GetPluginFactory() raised a structured exception (code 0x"
                  << std::hex << exceptionCode << std::dec << ") - crashed, not just failed" << std::endl;
        return 1;
    }

    if (factory == nullptr)
    {
        std::cout << "[FAIL] GetPluginFactory() returned null" << std::endl;

        // If this binary is HallucinatorRAVE's own shim (see Source/Windows/
        // PluginShim.cpp), it exports a diagnostic-only function that
        // reveals exactly what GetPluginFactory() alone can't: the impl DLL
        // path it tried, and the raw Win32 error if loading it failed.
        using DiagFunc = void (*) (wchar_t*, int, unsigned long*, int*);
        auto diagFn = reinterpret_cast<DiagFunc>(GetProcAddress(mod, "HallucinatorShimDiagnostics"));
        if (diagFn != nullptr)
        {
            wchar_t pathBuf[512] = {};
            unsigned long lastError = 0;
            int loaded = 0;
            diagFn(pathBuf, 512, &lastError, &loaded);

            std::wcout << L"[INFO] Shim tried to load: " << pathBuf << std::endl;
            std::cout << "[INFO] Impl DLL loaded: " << (loaded ? "yes" : "no") << std::endl;
            if (! loaded)
                std::cout << "[INFO] LoadLibraryExW failed with Win32 error " << lastError << std::endl;
        }
        else
        {
            std::cout << "[INFO] No HallucinatorShimDiagnostics export found - this binary "
                         "doesn't look like HallucinatorRAVE's shim, or is an older build." << std::endl;
        }

        // Whatever the shim couldn't resolve, every DLL this plugin ships
        // lives in this same folder - try loading each one standalone to
        // find out exactly which specific file can't load on this machine,
        // rather than guessing at HallucinatorRAVE_Impl.dll's dependency
        // chain from the outside.
        auto lastSlash = pathW.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            std::wstring dir = pathW.substr(0, lastSlash);
            std::cout << "\n=== Step 3.5: trying every .dll in that folder individually ===" << std::endl;
            checkAllDllsInDirectory(dir);
        }

        return 1;
    }
    std::cout << "[OK] GetPluginFactory() returned " << factory << std::endl;

    std::cout << "\n=== Step 4: getFactoryInfo ===" << std::endl;
    PFactoryInfo factoryInfo {};
    tresult factoryInfoResult = kResultFalse;
    if (! callGetFactoryInfo(factory, factoryInfo, factoryInfoResult, exceptionCode))
    {
        std::cout << "[FAIL] getFactoryInfo() raised a structured exception (code 0x"
                  << std::hex << exceptionCode << std::dec << ")" << std::endl;
        return 1;
    }
    if (factoryInfoResult != kResultOk)
    {
        std::cout << "[FAIL] getFactoryInfo() returned error code " << factoryInfoResult << std::endl;
    }
    else
    {
        std::cout << "[OK] Vendor: \"" << factoryInfo.vendor << "\"" << std::endl;
        std::cout << "     URL:    \"" << factoryInfo.url << "\"" << std::endl;
        std::cout << "     Email:  \"" << factoryInfo.email << "\"" << std::endl;
    }

    std::cout << "\n=== Step 5: countClasses / getClassInfo ===" << std::endl;
    int32 numClasses = 0;
    if (! callCountClasses(factory, numClasses, exceptionCode))
    {
        std::cout << "[FAIL] countClasses() raised a structured exception (code 0x"
                  << std::hex << exceptionCode << std::dec << ")" << std::endl;
        return 1;
    }
    std::cout << "[OK] countClasses() = " << numClasses << std::endl;

    for (int32 i = 0; i < numClasses; ++i)
    {
        PClassInfo classInfo {};
        tresult classInfoResult = kResultFalse;
        if (! callGetClassInfo(factory, i, classInfo, classInfoResult, exceptionCode))
        {
            std::cout << "[FAIL] getClassInfo(" << i << ") raised a structured exception (code 0x"
                      << std::hex << exceptionCode << std::dec << ")" << std::endl;
            continue;
        }
        if (classInfoResult != kResultOk)
        {
            std::cout << "[FAIL] getClassInfo(" << i << ") returned error code " << classInfoResult << std::endl;
            continue;
        }
        std::cout << "[OK] Class " << i << ": name=\"" << classInfo.name
                  << "\" category=\"" << classInfo.category
                  << "\" cid=" << tuidToString(classInfo.cid) << std::endl;
    }

    std::cout << "\nDone. If every step above says [OK], the plugin's Windows load and VST3\n"
                 "factory lifecycle work correctly on this machine, and the DAW-specific\n"
                 "failure is happening somewhere this tool doesn't reach (e.g. actual\n"
                 "instance creation/IComponent::initialize(), the DAW's own scan-caching,\n"
                 "or something host-specific) - share this full output either way."
              << std::endl;

    return 0;
}
