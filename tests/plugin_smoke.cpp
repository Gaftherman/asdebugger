// SPDX-License-Identifier: MIT
#include <extdll.h>
#include <meta_api.h>
#include <asext_api.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#ifndef _WIN32
#    include <dlfcn.h>
#endif

namespace
{
bool                          g_load = true, g_acceptDefine = true, g_acceptDoc = true;
const char*                   g_missing = "";
fnASDocInitCallback           g_doc     = nullptr;
fnScriptBuilderDefineCallback g_define  = nullptr;
int                           g_errors = 0, g_version = 0;
bool                          g_word = false;

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
void Log(plid_t, const char*, ...) {}
void Error(plid_t, const char*, ...) { ++g_errors; }
int  Load(plid_t, const char* path, PLUG_LOADTIME now, void** handle)
{
    Check(PT_STARTUP == now && std::strstr(path, "addons/metamod/dlls/asext."), "asext startup load");
    *handle = g_load ? reinterpret_cast<void*>(1) : nullptr;
    return g_load ? 0 : -1;
}
bool RegisterDoc(fnASDocInitCallback callback)
{
    if (g_acceptDoc)
        g_doc = callback;
    return g_acceptDoc;
}
bool RegisterDefine(fnScriptBuilderDefineCallback callback)
{
    if (g_acceptDefine)
        g_define = callback;
    return g_acceptDefine;
}
bool UnregisterDefine(fnScriptBuilderDefineCallback callback)
{
    Check(g_define == callback, "rollback removes the registered callback");
    g_define = nullptr;
    return true;
}
void RegisterFunction(CASDocumentation*, const char*, const char* declaration, void* function, int convention)
{
    Check(0 == std::strcmp("int ASTemplateVersion()", declaration), "script declaration");
    Check(asCALL_CDECL == convention, "script calling convention");
    g_version = reinterpret_cast<int (*)()>(function)();
}
void DefineWord(CScriptBuilder*, const char* word)
{
    g_word = 0 == std::strcmp("METAMOD_PLUGIN_ASTEMPLATE", word);
}
void* Resolve(DLHANDLE, const char* name)
{
    if (0 == std::strcmp(g_missing, name))
        return nullptr;
#define MOCK_EXPORT(symbol, function) \
    if (0 == std::strcmp(name, symbol)) return reinterpret_cast<void*>(function)
    MOCK_EXPORT("ASEXT_RegisterDocInitCallback", RegisterDoc);
    MOCK_EXPORT("ASEXT_RegisterScriptBuilderDefineCallback", RegisterDefine);
    MOCK_EXPORT("ASEXT_UnregisterScriptBuilderDefineCallback", UnregisterDefine);
    MOCK_EXPORT("ASEXT_RegisterGlobalFunction", RegisterFunction);
    MOCK_EXPORT("ASEXT_CScriptBuilder_DefineWord", DefineWord);
#undef MOCK_EXPORT
    return nullptr;
}
} // namespace

int main(int argc, char** argv)
{
    Check(2 == argc, "plugin path argument");
#ifdef _WIN32
    auto handle = LoadLibraryA(argv[1]);
    auto symbol = [handle](const char* name) {
        return reinterpret_cast<void*>(GetProcAddress(handle, name));
    };
#else
    auto handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    auto symbol = [handle](const char* name) {
        return dlsym(handle, name);
    };
#endif
    Check(nullptr != handle, "load native plugin");
    auto query  = reinterpret_cast<META_QUERY_FN>(symbol("Meta_Query"));
    auto attach = reinterpret_cast<META_ATTACH_FN>(symbol("Meta_Attach"));
    auto detach = reinterpret_cast<META_DETACH_FN>(symbol("Meta_Detach"));
    Check(query && attach && detach && symbol("GiveFnptrsToDll"), "required exported entrypoints");
    mutil_funcs_t utilities{};
    utilities.pfnLogMessage     = Log;
    utilities.pfnLogError       = Error;
    utilities.pfnLoadPlugin     = Load;
    utilities.pfnGetProcAddress = Resolve;
    plugin_info_t* info         = nullptr;
    for (const char* version : {"", "5", "5:", "5::17", "5:17x", "-5:17", "5:16", "6:17", "999999999999:17"})
        Check(FALSE == query(version, &info, &utilities), "reject incompatible interface");
    Check(FALSE == query(nullptr, &info, &utilities), "reject null version");
    Check(FALSE == query("5:17", nullptr, &utilities), "reject null info");
    Check(TRUE == query("5:17", &info, &utilities), "accept supported interface");
    Check(TRUE == query("5:18", &info, &utilities), "accept newer compatible minor");
    Check(PT_STARTUP == info->loadable && PT_NEVER == info->unloadable, "safe lifecycle flags");
    META_FUNCTIONS  functions{};
    meta_globals_t  globals{};
    gamedll_funcs_t game{};
    Check(FALSE == attach(PT_ANYTIME, &functions, &globals, &game), "reject late loading");
    Check(FALSE == attach(PT_STARTUP, nullptr, &globals, &game), "reject null table");
    g_load = false;
    Check(FALSE == attach(PT_STARTUP, &functions, &globals, &game), "reject missing dependency");
    g_load    = true;
    g_missing = "ASEXT_RegisterGlobalFunction";
    Check(FALSE == attach(PT_STARTUP, &functions, &globals, &game), "reject missing dependency export");
    g_missing      = "";
    g_acceptDefine = false;
    Check(FALSE == attach(PT_STARTUP, &functions, &globals, &game), "reject builder registration failure");
    g_acceptDefine = true;
    g_acceptDoc    = false;
    Check(FALSE == attach(PT_STARTUP, &functions, &globals, &game), "reject documentation registration failure");
    Check(nullptr == g_define, "registration failure rolls back builder callback");
    g_acceptDoc = true;
    Check(TRUE == attach(PT_STARTUP, &functions, &globals, &game), "attach plugin");
    Check(g_doc && g_define && functions.pfnGetEntityAPI2, "publish callbacks and hook table");
    g_doc(nullptr);
    g_define(nullptr);
    Check(1 == g_version && g_word, "invoke registered script function and define callback");
    DLL_FUNCTIONS hooks{};
    int           version = 0;
    Check(FALSE == functions.pfnGetEntityAPI2(&hooks, &version), "reject incorrect engine API version");
    Check(INTERFACE_VERSION == version, "report engine API version");
    Check(TRUE == functions.pfnGetEntityAPI2(&hooks, &version), "provide engine API table");
    Check(FALSE == attach(PT_STARTUP, &functions, &globals, &game), "reject duplicate attachment");
    Check(FALSE == detach(PT_ANYTIME, PNL_CMD_FORCED), "reject forced runtime unload");
    Check(TRUE == detach(PT_ANYTIME, PNL_SHUTDOWN), "allow shutdown");
    Check(g_errors >= 5, "failure paths log errors");
    std::puts("Plugin ABI smoke test passed (mock Metamod/ASExt; no game server).");
    return 0;
}
