#pragma once

#include <string>

void ASDebugger_Init(const char* gameDir);
void ASDebugger_ServerActivate(const char* gameDir);
void ASDebugger_Frame();
void ASDebugger_Shutdown();

void RegisterAngelScriptMethods();
void RegisterAngelScriptHooks();

void ASDebug_Notice(const char* fmt, ...);
void ASDebug_SetVerbose(bool enable);
bool ASDebug_IsVerbose();

extern int g_DebuggerPort;
