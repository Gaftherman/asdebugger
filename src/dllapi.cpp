#include <extdll.h>
#include <dllapi.h>
#include <meta_api.h>

#include "serverdef.h"
#include "asdebugger.h"

void NewStartFrame(void)
{
    ASDebugger_Frame();
    SET_META_RESULT(MRES_IGNORED);
}

void NewGameShutdown(void)
{
    ASDebugger_Shutdown();
    SET_META_RESULT(MRES_IGNORED);
}

void ServerActivate(edict_t* pEdictList, int edictCount, int clientMax)
{
    const char* gameDir = nullptr;
    if (gpMetaUtilFuncs)
        gameDir = GET_GAME_INFO(PLID, GINFO_GAMEDIR);
    ASDebugger_Init(gameDir);
    SET_META_RESULT(MRES_IGNORED);
}

static DLL_FUNCTIONS gFunctionTable =
    {
        NULL, // pfnGameInit
        NULL, // pfnSpawn
        NULL, // pfnThink
        NULL, // pfnUse
        NULL, // pfnTouch
        NULL, // pfnBlocked
        NULL, // pfnKeyValue
        NULL, // pfnSave
        NULL, // pfnRestore
        NULL, // pfnSetAbsBox

        NULL, // pfnSaveWriteFields
        NULL, // pfnSaveReadFields

        NULL, // pfnSaveGlobalState
        NULL, // pfnRestoreGlobalState
        NULL, // pfnResetGlobalState

        NULL,           // pfnClientConnect
        NULL,           // pfnClientDisconnect
        NULL,           // pfnClientKill
        NULL,           // pfnClientPutInServer
        NULL,           // pfnClientCommand
        NULL,           // pfnClientUserInfoChanged
        ServerActivate, // pfnServerActivate
        NULL,           // pfnServerDeactivate

        NULL, // pfnPlayerPreThink
        NULL, // pfnPlayerPostThink

        NewStartFrame, // pfnStartFrame
        NULL,          // pfnParmsNewLevel
        NULL,          // pfnParmsChangeLevel

        NULL, // pfnGetGameDescription
        NULL, // pfnPlayerCustomization

        NULL, // pfnSpectatorConnect
        NULL, // pfnSpectatorDisconnect
        NULL, // pfnSpectatorThink

        NULL, // pfnSys_Error

        NULL, // pfnPM_Move
        NULL, // pfnPM_Init
        NULL, // pfnPM_FindTextureType

        NULL, // pfnSetupVisibility
        NULL, // pfnUpdateClientData
        NULL, // pfnAddToFullPack
        NULL, // pfnCreateBaseline
        NULL, // pfnRegisterEncoders
        NULL, // pfnGetWeaponData
        NULL, // pfnCmdStart
        NULL, // pfnCmdEnd
        NULL, // pfnConnectionlessPacket
        NULL, // pfnGetHullBounds
        NULL, // pfnCreateInstancedBaselines
        NULL, // pfnInconsistentFile
        NULL, // pfnAllowLagCompensation
};

C_DLLEXPORT int GetEntityAPI2(DLL_FUNCTIONS* pFunctionTable, int* interfaceVersion)
{
    if (!pFunctionTable)
    {
        LOG_ERROR(PLID, "GetEntityAPI2 called with null pFunctionTable");
        return FALSE;
    }
    else if (*interfaceVersion != INTERFACE_VERSION)
    {
        LOG_ERROR(PLID, "GetEntityAPI2 version mismatch; requested=%d ours=%d", *interfaceVersion, INTERFACE_VERSION);
        *interfaceVersion = INTERFACE_VERSION;
        return FALSE;
    }

    memcpy(pFunctionTable, &gFunctionTable, sizeof(DLL_FUNCTIONS));
    return TRUE;
}

static NEW_DLL_FUNCTIONS gNewDllFunctionTable =
    {
        NULL,
        NewGameShutdown,
        NULL,
        NULL,
        NULL,
};

C_DLLEXPORT int GetNewDLLFunctions(NEW_DLL_FUNCTIONS* pNewDllFunctionTable, int* interfaceVersion)
{
    if (!pNewDllFunctionTable)
    {
        LOG_ERROR(PLID, "GetNewDLLFunctions called with null pFunctionTable");
        return FALSE;
    }
    else if (*interfaceVersion != NEW_DLL_FUNCTIONS_VERSION)
    {
        LOG_ERROR(PLID, "GetNewDLLFunctions version mismatch; requested=%d ours=%d", *interfaceVersion, NEW_DLL_FUNCTIONS_VERSION);
        *interfaceVersion = NEW_DLL_FUNCTIONS_VERSION;
        return FALSE;
    }

    memcpy(pNewDllFunctionTable, &gNewDllFunctionTable, sizeof(NEW_DLL_FUNCTIONS));
    return TRUE;
}
