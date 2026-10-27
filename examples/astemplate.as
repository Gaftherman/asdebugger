void PluginInit()
{
    g_Module.ScriptInfo.SetAuthor("astemplate contributors");
    g_Module.ScriptInfo.SetContactInfo("https://github.com/metamod-fallguys/astemplate");
#if METAMOD_PLUGIN_ASTEMPLATE
    g_Game.AlertMessage(at_console, "ASTemplate API version: %1\n", ASTemplateVersion());
#else
    g_Game.AlertMessage(at_console, "astemplate native plugin is not loaded.\n");
#endif
}
