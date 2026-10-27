#pragma once

#include <string>

void ASDebugger_Init(const char* gameDir);
void ASDebugger_ServerActivate(const char* gameDir);
void ASDebugger_Frame();
void ASDebugger_Shutdown();

void RegisterAngelScriptMethods();
void RegisterAngelScriptHooks();

extern int g_DebuggerPort;
