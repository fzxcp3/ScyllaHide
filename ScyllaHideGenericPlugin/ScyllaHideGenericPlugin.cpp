#include "ScyllaHideGenericPlugin.h"
#include <string>
#include <unordered_map>
#include <Scylla/NtApiLoader.h>
#include <Scylla/Settings.h>
#include <Scylla/Util.h>

#include "..\PluginGeneric\Injector.h"

typedef void(__cdecl * t_LogWrapper)(const WCHAR * format, ...);
typedef void(__cdecl * t_AttachProcess)(DWORD dwPID);

scl::Settings g_settings;

#ifdef _WIN64
const WCHAR g_scyllaHideDllFilename[] = L"HookLibraryx64.dll";
#else
const WCHAR g_scyllaHideDllFilename[] = L"HookLibraryx86.dll";
#endif

std::wstring g_scyllaHideDllPath;
std::wstring g_ntApiCollectionIniPath;
std::wstring g_scyllaHideIniPath;

extern HOOK_DLL_EXCHANGE DllExchangeLoader;
extern t_LogWrapper LogWrap;
extern t_LogWrapper LogErrorWrap;

//globals
static HMODULE hNtdllModule = 0;

struct HookStatus
{
    HookStatus()
        : ProcessId(0),
        bHooked(false),
        specialPebFix(false)
    {
    }

    DWORD ProcessId;
    bool bHooked;
    bool specialPebFix;
};

static std::unordered_map<DWORD, HookStatus> hookStatusMap;

DLL_EXPORT void ScyllaHideDebugLoop(const DEBUG_EVENT* DebugEvent)
{
    auto pid = DebugEvent->dwProcessId;
    auto status = HookStatus();
    auto found = hookStatusMap.find(pid);
    if (found == hookStatusMap.end())
        hookStatusMap[pid] = status;
    else
        status = hookStatusMap[pid];

    if (g_settings.opts().fixPebHeapFlags)
    {
        if (status.specialPebFix)
        {
            StartFixBeingDebugged(status.ProcessId, false);
            status.specialPebFix = false;
        }

        if (DebugEvent->u.LoadDll.lpBaseOfDll == hNtdllModule)
        {
            StartFixBeingDebugged(status.ProcessId, true);
            status.specialPebFix = true;
        }
    }

    switch (DebugEvent->dwDebugEventCode)
    {
    case CREATE_PROCESS_DEBUG_EVENT:
    {
        status.ProcessId = DebugEvent->dwProcessId;
        status.bHooked = false;
        ZeroMemory(&DllExchangeLoader, sizeof(HOOK_DLL_EXCHANGE));

        if (DebugEvent->u.CreateProcessInfo.lpStartAddress == NULL)
        {
            //ATTACH
            if (g_settings.opts().killAntiAttach)
            {
                if (!ApplyAntiAntiAttach(status.ProcessId))
                {
                    LogWrap(L"Anti-Anti-Attach failed");
                }
            }
        }

        break;
    }

    case LOAD_DLL_DEBUG_EVENT:
    {
        if (status.bHooked)
        {
            startInjection(status.ProcessId, g_scyllaHideDllPath.c_str(), false);
        }

        break;
    }

    case EXCEPTION_DEBUG_EVENT:
    {
        switch (DebugEvent->u.Exception.ExceptionRecord.ExceptionCode)
        {
        case STATUS_BREAKPOINT:
        {
            if (!status.bHooked)
            {
                ReadNtApiInformation(g_ntApiCollectionIniPath.c_str(), &DllExchangeLoader);

                status.bHooked = true;
                startInjection(status.ProcessId, g_scyllaHideDllPath.c_str(), true);
            }

            break;
        }
        }

        break;
    }
    }

    hookStatusMap[pid] = status;
}

DLL_EXPORT void ScyllaHideReset()
{
    ZeroMemory(&DllExchangeLoader, sizeof(HOOK_DLL_EXCHANGE));
    hookStatusMap.clear();
}

static void LogErrorWrapper(const WCHAR * format, ...)
{
    WCHAR text[2000];
    CHAR textA[2000];
    va_list va_alist;
    va_start(va_alist, format);

    wvsprintfW(text, format, va_alist);

    WideCharToMultiByte(CP_ACP, 0, text, -1, textA, _countof(textA), 0, 0);

    printf("%s\n", textA);
}

static void LogWrapper(const WCHAR * format, ...)
{
    WCHAR text[2000];
    CHAR textA[2000];
    va_list va_alist;
    va_start(va_alist, format);

    wvsprintfW(text, format, va_alist);

    WideCharToMultiByte(CP_ACP, 0, text, -1, textA, _countof(textA), 0, 0);

    printf("%s\n", textA);
}

DLL_EXPORT void ScyllaHideInit(const WCHAR* Directory, LOGWRAPPER Logger, LOGWRAPPER ErrorLogger)
{
    //Set log functions
    if (!Logger)
        LogWrap = LogWrapper;
    else
        LogWrap = Logger;
    if (!ErrorLogger)
        LogErrorWrap = LogErrorWrapper;
    else
        LogErrorWrap = ErrorLogger;

    //Load paths
    hNtdllModule = GetModuleHandleW(L"ntdll.dll");

    std::wstring wstrPath;

    if (Directory)
    {
        wstrPath = Directory;
        if (wstrPath.back() != L'\\')
            wstrPath += L'\\';
    } else
    {
        wstrPath = scl::GetModuleFileNameW();
        wstrPath.resize(wstrPath.find_last_of(L'\\') + 1);
    }

    g_scyllaHideDllPath = wstrPath + g_scyllaHideDllFilename;
    g_ntApiCollectionIniPath = wstrPath + scl::NtApiLoader::kFileName;
    g_scyllaHideIniPath = wstrPath + scl::Settings::kFileName;

    g_settings.Load(g_scyllaHideIniPath.c_str());
}
