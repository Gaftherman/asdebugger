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
void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
void Log(plid_t, const char*, ...) {}
void Error(plid_t, const char*, ...) {}
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
    auto fnptrs = symbol("GiveFnptrsToDll");
    Check(query && attach && detach && fnptrs, "required exported entrypoints");

    mutil_funcs_t utilities{};
    utilities.pfnLogMessage = Log;
    utilities.pfnLogError   = Error;

    plugin_info_t* info = nullptr;
    Check(FALSE == query(nullptr, &info, &utilities), "reject null version");
    Check(FALSE == query("5:17", nullptr, &utilities), "reject null info");
    Check(TRUE == query("5:17", &info, &utilities), "accept supported interface");
    Check(nullptr != info, "valid plugin info");
    Check(0 == std::strcmp(info->name, "AngelScriptDebugger"), "matching plugin name");

    std::puts("Plugin ABI smoke test passed.");
    return 0;
}
