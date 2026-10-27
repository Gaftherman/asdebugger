// SPDX-License-Identifier: MIT
// Minimal Metamod/ASExt plugin; see THIRD_PARTY.md for SDK notices.
#include <extdll.h>
#include <h_export.h>
#include <meta_api.h>
#include <asext_api.h>

#include <charconv>
#include <cstring>

enginefuncs_t    g_engfuncs{};
globalvars_t*    gpGlobals       = nullptr;
meta_globals_t*  gpMetaGlobals   = nullptr;
gamedll_funcs_t* gpGamedllFuncs  = nullptr;
mutil_funcs_t*   gpMetaUtilFuncs = nullptr;

plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION, "AngelScript Template", ASTEMPLATE_VERSION, __DATE__,
    "astemplate contributors", "https://github.com/metamod-fallguys/astemplate",
    "ASTEMPLATE", PT_STARTUP, PT_NEVER};

namespace
{
constexpr int                                 kMinimumInterfaceMajor = 5;
constexpr int                                 kMinimumInterfaceMinor = 17;
constexpr int                                 kScriptApiVersion      = 1;
bool                                          g_attached             = false;
fnASEXT_RegisterDocInitCallback               g_registerDoc          = nullptr;
fnASEXT_RegisterScriptBuilderDefineCallback   g_registerDefine       = nullptr;
fnASEXT_UnregisterScriptBuilderDefineCallback g_unregisterDefine     = nullptr;
fnASEXT_RegisterGlobalFunction                g_registerFunction     = nullptr;
fnASEXT_CScriptBuilder_DefineWord             g_defineWord           = nullptr;

bool CompatibleInterface(const char* version)
{
    if (!version)
        return false;
    const char* end   = version + std::strlen(version);
    int         major = 0, minor = 0;
    const auto  first = std::from_chars(version, end, major);
    if (first.ec != std::errc{} || first.ptr == end || *first.ptr != ':')
        return false;
    const auto second = std::from_chars(first.ptr + 1, end, minor);
    return second.ec == std::errc{} && second.ptr == end && major == kMinimumInterfaceMajor && minor >= kMinimumInterfaceMinor;
}

int ASTemplateVersion()
{
    return kScriptApiVersion;
}

void RegisterDocumentation(CASDocumentation* documentation)
{
    g_registerFunction(documentation, "Return the template API version.",
                       "int ASTemplateVersion()", reinterpret_cast<void*>(ASTemplateVersion), asCALL_CDECL);
}

void DefinePlugin(CScriptBuilder* builder)
{
    g_defineWord(builder, "METAMOD_PLUGIN_ASTEMPLATE");
}

template <typename Function>
bool ImportFunction(void* handle, const char* name, Function& function)
{
    function = reinterpret_cast<Function>(gpMetaUtilFuncs->pfnGetProcAddress(static_cast<DLHANDLE>(handle), name));
    if (!function)
    {
        LOG_ERROR(PLID, "Missing asext export: %s", name);
        return false;
    }
    return true;
}
} // namespace

C_DLLEXPORT void WINAPI GiveFnptrsToDll(enginefuncs_t* functions, globalvars_t* globals)
{
    if (functions)
        g_engfuncs = *functions;
    gpGlobals = globals;
}

C_DLLEXPORT int Meta_Query(const char* version, plugin_info_t** info, mutil_funcs_t* utilities)
{
    if (!info || !utilities || !utilities->pfnLogError || !utilities->pfnLogMessage)
        return FALSE;
    if (!CompatibleInterface(version))
    {
        utilities->pfnLogError(PLID, "Requires Metamod interface 5:17 or a newer 5.x minor version.");
        return FALSE;
    }
    gpMetaUtilFuncs = utilities;
    *info           = &Plugin_info;
    return TRUE;
}

C_DLLEXPORT int GetEntityAPI2(DLL_FUNCTIONS* functions, int* version)
{
    if (!functions || !version)
        return FALSE;
    if (*version != INTERFACE_VERSION)
    {
        *version = INTERFACE_VERSION;
        return FALSE;
    }
    // An empty hook table is sufficient for a plugin that only registers script APIs.
    *functions = {};
    return TRUE;
}

C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME now, META_FUNCTIONS* functions, meta_globals_t* globals, gamedll_funcs_t* gameFunctions)
{
    if (!gpMetaUtilFuncs || !functions || !globals || !gameFunctions)
        return FALSE;
    if (now != PT_STARTUP || g_attached)
    {
        LOG_ERROR(PLID, "Load astemplate once at server startup, before AngelScript initialization.");
        return FALSE;
    }
    if (!gpMetaUtilFuncs->pfnLoadPlugin || !gpMetaUtilFuncs->pfnGetProcAddress)
    {
        LOG_ERROR(PLID, "Metamod dependency loading utilities are unavailable.");
        return FALSE;
    }
    void* handle = nullptr;
#ifdef _WIN32
    constexpr const char* kASExtPath = "addons/metamod/dlls/asext.dll";
#else
    constexpr const char* kASExtPath = "addons/metamod/dlls/asext.so";
#endif
    LOAD_PLUGIN(PLID, kASExtPath, PT_STARTUP, &handle);
    if (!handle)
    {
        LOG_ERROR(PLID, "Cannot load %s. Install asext and its gamedata first.", kASExtPath);
        return FALSE;
    }
    if (!ImportFunction(handle, "ASEXT_RegisterDocInitCallback", g_registerDoc) || !ImportFunction(handle, "ASEXT_RegisterScriptBuilderDefineCallback", g_registerDefine) || !ImportFunction(handle, "ASEXT_UnregisterScriptBuilderDefineCallback", g_unregisterDefine) || !ImportFunction(handle, "ASEXT_RegisterGlobalFunction", g_registerFunction) || !ImportFunction(handle, "ASEXT_CScriptBuilder_DefineWord", g_defineWord))
        return FALSE;

    if (!g_registerDefine(DefinePlugin))
    {
        LOG_ERROR(PLID, "Cannot register the script builder callback.");
        return FALSE;
    }
    if (!g_registerDoc(RegisterDocumentation))
    {
        g_unregisterDefine(DefinePlugin);
        LOG_ERROR(PLID, "Cannot register documentation: AngelScript may already be initialized.");
        return FALSE;
    }
    gpMetaGlobals               = globals;
    gpGamedllFuncs              = gameFunctions;
    *functions                  = {};
    functions->pfnGetEntityAPI2 = GetEntityAPI2;
    g_attached                  = true;
    LOG_MESSAGE(PLID, "Registered ASTemplateVersion(); API version %d.", kScriptApiVersion);
    return TRUE;
}

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME, PL_UNLOAD_REASON reason)
{
    // ASExt cannot remove documentation callbacks or registered native functions.
    return reason == PNL_SHUTDOWN ? TRUE : FALSE;
}
