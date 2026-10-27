// scripts/plugins/asdebugger_example.as
// Sample AngelScript plugin for testing with the Sven Co-op AngelScript DAP Debugger

int g_ExecutionCount = 0;
string g_Status = "Running";

void PluginInit()
{
    g_Module.ScriptInfo.SetAuthor( "Gaftherman" );
    g_Module.ScriptInfo.SetContactInfo( "https://github.com/Gaftherman/asdebugger" );

#if METAMOD_PLUGIN_ASDEBUGGER
    g_Game.AlertMessage( at_console, "[ASDEBUG] asdebugger native plugin is active! DAP server on port 9002.\n" );
#else
    g_Game.AlertMessage( at_console, "[ASDEBUG] asdebugger native plugin is NOT loaded.\n" );
#endif

    // Schedule a recurring test function every 3 seconds
    g_Scheduler.SetInterval( "PeriodicDebugTask", 3.0f, g_Scheduler.REPEAT_INFINITE_TIMES );
}

void PeriodicDebugTask()
{
    int testInt = 42;
    float testFloat = 3.14159f;
    string debugMessage = "Inspecting variables in VS Code";
    Vector playerSpawn( 0.0f, 64.0f, -128.0f );

    g_ExecutionCount++;

    // You can place a breakpoint on the line below in VS Code (F9),
    // or trigger an explicit programmatic breakpoint with DebugBreak():
    // #if METAMOD_PLUGIN_ASDEBUGGER
    //     DebugBreak();
    // #endif

    g_EngineFuncs.ServerPrint( "[ASDEBUG-SAMPLE] Execution #" + g_ExecutionCount + 
                               " | testInt=" + testInt + 
                               " | testFloat=" + testFloat + 
                               " | pos=" + playerSpawn.ToString() + "\n" );
}
