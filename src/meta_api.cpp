// meta_api.cpp - Metamod plugin interface for Sven Co-op AngelScript DAP Debugger

#include <extdll.h>
#include <meta_api.h>
#include "sdk_util.h"

#include "serverdef.h"
#include "asdebugger.h"

#ifdef _WIN32
#    include <windows.h>
#    include <dbghelp.h>

static LPTOP_LEVEL_EXCEPTION_FILTER g_PrevExceptionFilter = nullptr;

static LONG WINAPI ASDebugger_UnhandledCrashHandler(EXCEPTION_POINTERS* pExceptionInfo)
{
    char       dumpPath[MAX_PATH];
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(dumpPath, sizeof(dumpPath), "svencoop/addons/metamod/asdebugger_crash_%04d%02d%02d_%02d%02d%02d.mdmp",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileA(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId          = GetCurrentThreadId();
        mdei.ExceptionPointers = pExceptionInfo;
        mdei.ClientPointers    = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mdei, NULL, NULL);
        CloseHandle(hFile);
        printf("[ASDEBUG] Crash minidump written to %s\n", dumpPath);
    }

    if (g_PrevExceptionFilter)
        return g_PrevExceptionFilter(pExceptionInfo);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

IMPORT_ASEXT_API_DEFINE();

// Plugin function tables provided to Metamod
static META_FUNCTIONS gMetaFunctionTable = {
    NULL,               // pfnGetEntityAPI              HL SDK; called before game DLL
    NULL,               // pfnGetEntityAPI_Post         META; called after game DLL
    GetEntityAPI2,      // pfnGetEntityAPI2             HL SDK2; called before game DLL
    NULL,               // pfnGetEntityAPI2_Post        META; called after game DLL
    GetNewDLLFunctions, // pfnGetNewDLLFunctions        HL SDK2; called before game DLL
    NULL,               // pfnGetNewDLLFunctions_Post   META; called after game DLL
    NULL,               // pfnGetEngineFunctions        META; called before HL engine
    NULL,               // pfnGetEngineFunctions_Post   META; called after HL engine
    NULL,               // pfnGetStudioBlendingInterface
    NULL,               // pfnGetStudioBlendingInterface_Post
};

// Description of plugin
plugin_info_t Plugin_info = {
    "5:14",                                     // ifvers
    "AngelScriptDebugger",                      // name
    "1.4.0",                                    // version
    "2026",                                     // date
    "Gaftherman",                               // author
    "https://github.com/Gaftherman/asdebugger", // url
    "ASDEBUG",                                  // logtag, all caps please
    PT_ANYTIME,                                 // (when) loadable
    PT_ANYTIME,                                 // (when) unloadable
};

// Global vars from metamod:
meta_globals_t*  gpMetaGlobals;
gamedll_funcs_t* gpGamedllFuncs;
mutil_funcs_t*   gpMetaUtilFuncs;

static bool ParseInterfaceVersion(const char* interfaceVersion, int* pMajorVersion, int* pMinorVersion)
{
    if (!interfaceVersion || !pMajorVersion || !pMinorVersion)
        return false;

    char* endPtr   = nullptr;
    *pMajorVersion = strtol(interfaceVersion, &endPtr, 10);

    if (endPtr == interfaceVersion || (*endPtr != '\0' && *endPtr != ':'))
        return false;

    if (*endPtr == ':')
    {
        *pMinorVersion = strtol(endPtr + 1, &endPtr, 10);
        if (endPtr == interfaceVersion + 1 || *endPtr != '\0')
            return false;
    }
    else
    {
        *pMinorVersion = 0;
    }

    return true;
}

C_DLLEXPORT int Meta_Query(const char* interfaceVersion, plugin_info_t** pPlugInfo, mutil_funcs_t* pMetaUtilFuncs)
{
    if (!interfaceVersion || !pPlugInfo || !pMetaUtilFuncs)
        return FALSE;

    int iMajorVersion = 0, iMinorVersion = 0;
    if (!ParseInterfaceVersion(interfaceVersion, &iMajorVersion, &iMinorVersion))
    {
        if (pMetaUtilFuncs->pfnLogError)
            pMetaUtilFuncs->pfnLogError(PLID, "Meta_Query failed to parse version string \"%s\"!", interfaceVersion);
        return FALSE;
    }

    if (iMajorVersion < 5 || (iMajorVersion == 5 && iMinorVersion < 14))
    {
        if (pMetaUtilFuncs->pfnLogError)
            pMetaUtilFuncs->pfnLogError(PLID, "Meta_Query version too low! expect \"%s\" but got \"%s\"", "5:14", interfaceVersion);
        return FALSE;
    }

    *pPlugInfo      = &Plugin_info;
    gpMetaUtilFuncs = pMetaUtilFuncs;
    return TRUE;
}

C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME /* now */,
                            META_FUNCTIONS*  pFunctionTable,
                            meta_globals_t*  pMGlobals,
                            gamedll_funcs_t* pGamedllFuncs)
{
    if (!pMGlobals)
    {
        LOG_ERROR(PLID, "Meta_Attach called with null pMGlobals");
        return FALSE;
    }

    gpMetaGlobals = pMGlobals;

    if (!pFunctionTable)
    {
        LOG_ERROR(PLID, "Meta_Attach called with null pFunctionTable");
        return FALSE;
    }

    memcpy(pFunctionTable, &gMetaFunctionTable, sizeof(META_FUNCTIONS));
    gpGamedllFuncs = pGamedllFuncs;

    auto engineHandle = gpMetaUtilFuncs->pfnGetEngineHandle();
    auto engineBase   = gpMetaUtilFuncs->pfnGetEngineBase();

    if (!engineHandle || !engineBase)
    {
        LOG_ERROR(PLID, "Engine handle or base not found!");
        return FALSE;
    }

    void* asextHandle = NULL;

#ifdef _WIN32
    LOAD_PLUGIN(PLID, "addons/metamod/dlls/asext.dll", PLUG_LOADTIME::PT_ANYTIME, &asextHandle);
#else
    LOAD_PLUGIN(PLID, "addons/metamod/dlls/asext.so", PLUG_LOADTIME::PT_ANYTIME, &asextHandle);
#endif
    if (!asextHandle)
    {
        LOG_ERROR(PLID, "asext dll handle not found! Make sure asext is installed in addons/metamod/dlls/");
        return FALSE;
    }

    IMPORT_ASEXT_API(asext);

    RegisterAngelScriptMethods();
    RegisterAngelScriptHooks();

    const char* gameDir = GET_GAME_INFO(PLID, GINFO_GAMEDIR);
    ASDebugger_Init(gameDir);

#ifdef _WIN32
    g_PrevExceptionFilter = SetUnhandledExceptionFilter(ASDebugger_UnhandledCrashHandler);
#endif

    return TRUE;
}

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME /* now */, PL_UNLOAD_REASON /* reason */)
{
#ifdef _WIN32
    if (g_PrevExceptionFilter)
    {
        SetUnhandledExceptionFilter(g_PrevExceptionFilter);
        g_PrevExceptionFilter = nullptr;
    }
#endif
    ASDebugger_Shutdown();
    return TRUE;
}
