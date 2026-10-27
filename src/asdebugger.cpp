#include <extdll.h>
#include <meta_api.h>

#include "serverdef.h"
#include "asdebugger.h"
#include "as_sc_debugger.h"
#include "as_sc_workspace.h"
#include "as_debugger_dap.h"

#include <memory>
#include <string>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/mman.h>
#    include <unistd.h>
#    include <csignal>
#endif

int g_DebuggerPort = 9002;

static std::unique_ptr<asSCWorkspace>  g_Workspace;
static std::unique_ptr<asSCDebugger>   g_Debugger;
static std::unique_ptr<asIDBDAPServer> g_DAPServer;

#ifdef _WIN32
typedef int(__thiscall* fnExecute)(asIScriptContext* pThis);
#else
typedef int (*fnExecute)(asIScriptContext* pThis);
#endif
static fnExecute g_pfnOriginalExecute = nullptr;
static void**    g_pContextVtable     = nullptr;

void ASDebug_Log(const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    if (gpMetaUtilFuncs && gpMetaUtilFuncs->pfnLogMessage)
    {
        LOG_MESSAGE(PLID, "%s", buf);
    }
    printf("[ASDEBUG] %s\n", buf);
    fflush(stdout);
}

static int s_ExecuteCount = 0;
#ifdef _WIN32
static int __fastcall Hooked_Execute(asIScriptContext* pThis, void* /*dummy*/)
#else
static int Hooked_Execute(asIScriptContext* pThis)
#endif
{
    // Fast-path: if no debugger work is pending, bypass all mutex locks
    if (g_Debugger && g_Debugger->has_work_atomic.load(std::memory_order_relaxed))
    {
        if (g_Debugger->HasWork())
        {
            asIScriptFunction* fn = pThis->GetFunction(0);
            ASDebug_Log("Hooked_Execute HAS WORK: ctx=%p, func=%s, state=%d",
                        pThis, fn ? fn->GetName() : "null", (int)pThis->GetState());
            g_Debugger->HookContext(pThis, true);
        }
    }

    int ret = g_pfnOriginalExecute(pThis);

    if (g_Debugger && g_Debugger->has_work_atomic.load(std::memory_order_relaxed))
    {
        std::scoped_lock lock(g_Debugger->mutex);
        if (pThis == g_Debugger->stepping_context &&
            (pThis->GetState() == asEXECUTION_FINISHED || pThis->GetState() == asEXECUTION_UNINITIALIZED))
        {
            g_Debugger->action               = asIDBAction::None;
            g_Debugger->stepping_context     = nullptr;
            g_Debugger->stepping_function_id = -1;
            g_Debugger->UpdateHasWorkAtomic();
        }
    }

    return ret;
}

static void HookContextVtable(asIScriptEngine* engine)
{
    if (g_pfnOriginalExecute || !engine)
        return;

    asIScriptContext* dummy = engine->CreateContext();
    if (!dummy)
    {
        LOG_ERROR(PLID, "Failed to create dummy context to hook vtable!");
        return;
    }

    g_pContextVtable = *(void***)dummy;

    if (g_pContextVtable)
    {
        // Execute is virtual method index 5 in asIScriptContext
        void** pSlot = &g_pContextVtable[5];

#ifdef _WIN32
        DWORD oldProtect = 0;
        if (VirtualProtect(pSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            g_pfnOriginalExecute = (fnExecute)*pSlot;
            *pSlot               = (void*)&Hooked_Execute;
            VirtualProtect(pSlot, sizeof(void*), oldProtect, &oldProtect);
            LOG_MESSAGE(PLID, "asIScriptContext::Execute successfully hooked via vtable slot 5 (%p -> %p)!",
                        g_pfnOriginalExecute, &Hooked_Execute);
        }
        else
        {
            LOG_ERROR(PLID, "VirtualProtect failed on context vtable!");
        }
#else
        uintptr_t page_start = ((uintptr_t)pSlot) & ~(sysconf(_SC_PAGE_SIZE) - 1);
        if (mprotect((void*)page_start, sysconf(_SC_PAGE_SIZE), PROT_READ | PROT_WRITE | PROT_EXEC) == 0)
        {
            g_pfnOriginalExecute = (fnExecute)*pSlot;
            *pSlot               = (void*)&Hooked_Execute;
            LOG_MESSAGE(PLID, "asIScriptContext::Execute successfully hooked via vtable slot 5 (%p -> %p)!",
                        g_pfnOriginalExecute, &Hooked_Execute);
        }
        else
        {
            LOG_ERROR(PLID, "mprotect failed on context vtable!");
        }
#endif
    }

    dummy->Release();
}

static void UnhookContextVtable()
{
    if (g_pfnOriginalExecute && g_pContextVtable)
    {
        void** pSlot = &g_pContextVtable[5];
#ifdef _WIN32
        DWORD oldProtect = 0;
        if (VirtualProtect(pSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            *pSlot = (void*)g_pfnOriginalExecute;
            VirtualProtect(pSlot, sizeof(void*), oldProtect, &oldProtect);
        }
#else
        uintptr_t page_start = ((uintptr_t)pSlot) & ~(sysconf(_SC_PAGE_SIZE) - 1);
        if (mprotect((void*)page_start, sysconf(_SC_PAGE_SIZE), PROT_READ | PROT_WRITE | PROT_EXEC) == 0)
        {
            *pSlot = (void*)g_pfnOriginalExecute;
        }
#endif
        g_pfnOriginalExecute = nullptr;
        g_pContextVtable     = nullptr;
    }
}

void ASDebugger_ServerActivate(const char* gameDir)
{
    if (!g_Debugger)
    {
        ASDebugger_Init(gameDir);
    }
    else if (g_Workspace)
    {
        g_Workspace->CompileScriptSources();
        g_Workspace->CompileBreakpointPositions();
    }
}

void ASDebugger_Init(const char* gameDir)
{
    if (g_Debugger)
        return;

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    auto srvMgr = ASEXT_GetServerManager();
    if (!srvMgr)
    {
        return;
    }

    asIScriptEngine* engine = srvMgr->GetScriptEngine();
    if (!engine)
    {
        return;
    }

    const char* envPort = getenv("ASDEBUGGER_PORT");
    if (envPort && *envPort)
    {
        int p = atoi(envPort);
        if (p > 0 && p < 65536)
            g_DebuggerPort = p;
    }

    std::string basePath = (gameDir && *gameDir) ? gameDir : "svencoop";

    g_Workspace            = std::make_unique<asSCWorkspace>(basePath, std::initializer_list<asIScriptEngine*>{engine});
    g_Debugger             = std::make_unique<asSCDebugger>(g_Workspace.get());
    g_DAPServer            = std::make_unique<asIDBDAPServer>(g_DebuggerPort, g_Debugger.get());
    g_Debugger->dap_server = g_DAPServer.get();

    // Hook context execution
    HookContextVtable(engine);

    // Start DAP server
    g_DAPServer->StartServer();
    if (g_DAPServer->IsRunning())
    {
        if (g_DAPServer->GetBoundPort() != g_DebuggerPort)
        {
            LOG_MESSAGE(PLID, "WARNING: Default DAP port %d is in use! Switched to fallback port %d. Set \"port\": %d in VS Code launch.json to attach.",
                        g_DebuggerPort, g_DAPServer->GetBoundPort(), g_DAPServer->GetBoundPort());
        }
        else
        {
            LOG_MESSAGE(PLID, "AngelScript DAP Server started on port %d (base path: '%s')",
                        g_DAPServer->GetBoundPort(), basePath.c_str());
        }
    }
    else
    {
        LOG_MESSAGE(PLID, "AngelScript DAP Server could not be started on port %d (port in use); debugger is idle and game continues smoothly.",
                    g_DebuggerPort);
    }
}

void ASDebugger_Frame()
{
    if (!g_Debugger)
    {
        const char* gameDir = nullptr;
        if (gpMetaUtilFuncs)
            gameDir = GET_GAME_INFO(PLID, GINFO_GAMEDIR);
        ASDebugger_Init(gameDir);
    }

    if (g_DAPServer)
    {
        g_DAPServer->Tick();
    }
}

void ASDebugger_Shutdown()
{
    LOG_MESSAGE(PLID, "Shutting down AngelScript DAP Server...");

    UnhookContextVtable();

    if (g_DAPServer)
    {
        g_DAPServer->StopServer();
        g_DAPServer.reset();
    }

    if (g_Debugger)
    {
        g_Debugger.reset();
    }

    if (g_Workspace)
    {
        g_Workspace.reset();
    }
}
