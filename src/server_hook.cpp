#include <extdll.h>
#include <dllapi.h>
#include <meta_api.h>

#include "serverdef.h"
#include "asdebugger.h"

static void AS_DebugBreak()
{
    void* ctxPtr = ASEXT_GetCurrentContext();
    if (ctxPtr)
    {
        // Force break on current context if needed
        LOG_MESSAGE(PLID, "AngelScript requested DebugBreak() from script context %p!", ctxPtr);
    }
}

void RegisterAngelScriptMethods()
{
    ASEXT_RegisterScriptBuilderDefineCallback([](CScriptBuilder* pScriptBuilder) {
        ASEXT_CScriptBuilder_DefineWord(pScriptBuilder, "METAMOD_PLUGIN_ASDEBUGGER");
    });

    ASEXT_RegisterDocInitCallback([](CASDocumentation* pASDoc) {
        ASEXT_RegisterGlobalFunction(pASDoc, "Trigger debugger break from script", "void DebugBreak()",
                                     (void*)AS_DebugBreak, asCALL_CDECL);
    });
}

void RegisterAngelScriptHooks()
{
}
