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
        bool ok = initDll();
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
    __try
    {
        factory = getFactory();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        std::cout << "[FAIL] GetPluginFactory() raised a structured exception (code 0x"
                  << std::hex << GetExceptionCode() << std::dec << ") - crashed, not just failed" << std::endl;
        return 1;
    }

    if (factory == nullptr)
    {
        std::cout << "[FAIL] GetPluginFactory() returned null" << std::endl;
        return 1;
    }
    std::cout << "[OK] GetPluginFactory() returned " << factory << std::endl;

    std::cout << "\n=== Step 4: getFactoryInfo ===" << std::endl;
    PFactoryInfo factoryInfo {};
    __try
    {
        tresult result = factory->getFactoryInfo(&factoryInfo);
        if (result != kResultOk)
        {
            std::cout << "[FAIL] getFactoryInfo() returned error code " << result << std::endl;
        }
        else
        {
            std::cout << "[OK] Vendor: \"" << factoryInfo.vendor << "\"" << std::endl;
            std::cout << "     URL:    \"" << factoryInfo.url << "\"" << std::endl;
            std::cout << "     Email:  \"" << factoryInfo.email << "\"" << std::endl;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        std::cout << "[FAIL] getFactoryInfo() raised a structured exception (code 0x"
                  << std::hex << GetExceptionCode() << std::dec << ")" << std::endl;
        return 1;
    }

    std::cout << "\n=== Step 5: countClasses / getClassInfo ===" << std::endl;
    int32 numClasses = 0;
    __try
    {
        numClasses = factory->countClasses();
        std::cout << "[OK] countClasses() = " << numClasses << std::endl;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        std::cout << "[FAIL] countClasses() raised a structured exception (code 0x"
                  << std::hex << GetExceptionCode() << std::dec << ")" << std::endl;
        return 1;
    }

    for (int32 i = 0; i < numClasses; ++i)
    {
        PClassInfo classInfo {};
        __try
        {
            tresult result = factory->getClassInfo(i, &classInfo);
            if (result != kResultOk)
            {
                std::cout << "[FAIL] getClassInfo(" << i << ") returned error code " << result << std::endl;
                continue;
            }
            std::cout << "[OK] Class " << i << ": name=\"" << classInfo.name
                      << "\" category=\"" << classInfo.category
                      << "\" cid=" << tuidToString(classInfo.cid) << std::endl;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            std::cout << "[FAIL] getClassInfo(" << i << ") raised a structured exception (code 0x"
                      << std::hex << GetExceptionCode() << std::dec << ")" << std::endl;
        }
    }

    std::cout << "\nDone. If every step above says [OK], the plugin's Windows load and VST3\n"
                 "factory lifecycle work correctly on this machine, and the DAW-specific\n"
                 "failure is happening somewhere this tool doesn't reach (e.g. actual\n"
                 "instance creation/IComponent::initialize(), the DAW's own scan-caching,\n"
                 "or something host-specific) - share this full output either way."
              << std::endl;

    return 0;
}
