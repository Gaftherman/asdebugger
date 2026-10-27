#include "as_sc_workspace.h"
#include <filesystem>
#include <fstream>
#include <system_error>

asSCWorkspace::asSCWorkspace(std::string_view basePath, std::initializer_list<asIScriptEngine*> engines) :
    asIDBWorkspace(engines),
    base_path(basePath)
{
    // Normalize base_path slashes
    std::replace(base_path.begin(), base_path.end(), '\\', '/');
    if (!base_path.empty() && base_path.back() == '/')
        base_path.pop_back();

    for (auto& engine : engines)
    {
        if (engine)
            this->engines.insert(engine);
    }

    CompileScriptSources();
    CompileBreakpointPositions();
}

std::string asSCWorkspace::PathToSection(const std::string_view v) const
{
    std::string pathStr(v);
    std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

    // 1. Exact match with an existing section (e.g. absolute engine path)
    if (sections.find(pathStr) != sections.end())
        return pathStr;

    // 2. Try relative path computation
    std::error_code       ec;
    std::filesystem::path fullPath(pathStr);
    std::filesystem::path base(base_path);

    std::filesystem::path rel    = std::filesystem::relative(fullPath, base, ec);
    std::string           relStr = ec ? pathStr : rel.generic_string();

    if (sections.find(relStr) != sections.end())
        return relStr;

    // 3. Try with "scripts/" prefix
    std::string withScripts = "scripts/" + relStr;
    if (sections.find(withScripts) != sections.end())
        return withScripts;

    // 4. Try with base_path prefix
    std::string withBase = base_path + "/" + relStr;
    if (sections.find(withBase) != sections.end())
        return withBase;

    // 5. Try stripped "scripts/" prefix
    if (relStr.rfind("scripts/", 0) == 0)
    {
        std::string stripped = relStr.substr(8);
        if (sections.find(stripped) != sections.end())
            return stripped;
    }

    // 6. Match by suffix (e.g. if a section ends with relStr)
    for (const auto& sec : sections)
    {
        if (sec.section.length() >= relStr.length() &&
            sec.section.compare(sec.section.length() - relStr.length(), relStr.length(), relStr) == 0)
        {
            return std::string(sec.section);
        }
    }

    return pathStr;
}

std::string asSCWorkspace::SectionToPath(const std::string_view v) const
{
    if (v.empty() || v.rfind("__", 0) == 0)
        return {};

    std::string sectionStr(v);
    std::replace(sectionStr.begin(), sectionStr.end(), '\\', '/');

    std::error_code ec;
    // 1. If sectionStr is already an existing file, return it directly
    if (std::filesystem::exists(sectionStr, ec))
        return sectionStr;

    // 2. Direct path from base_path
    std::string fullPath = base_path + "/" + sectionStr;
    if (std::filesystem::exists(fullPath, ec))
        return fullPath;

    // 3. Try inside scripts/ if not already starting with scripts/
    if (sectionStr.rfind("scripts/", 0) != 0)
    {
        std::string scriptsPath = base_path + "/scripts/" + sectionStr;
        if (std::filesystem::exists(scriptsPath, ec))
            return scriptsPath;
    }

    return {};
}

bool asSCWorkspace::SectionIsVirtual(const std::string_view v) const
{
    if (v.empty())
        return true;

    // Sections starting with "__" (e.g. __Global__BaseAnimating, __Shared__, etc.)
    if (v.rfind("__", 0) == 0)
        return true;

    std::string path = SectionToPath(v);
    if (path.empty())
        return true;

    std::error_code ec;
    return !std::filesystem::exists(path, ec);
}

std::string asSCWorkspace::SectionSource(const std::string_view v) const
{
    if (SectionIsVirtual(v))
    {
        return std::string("// [Virtual Engine Binding: ") + std::string(v) +
            "]\n// Native class or interface wrapper provided in-memory by Sven Co-op engine.\n";
    }

    std::string resolvedPath = SectionToPath(v);
    if (resolvedPath.empty())
        return {};

    std::error_code       ec;
    std::filesystem::path p(resolvedPath);

    if (!std::filesystem::exists(p, ec))
        return {};

    size_t size = (size_t)std::filesystem::file_size(p, ec);
    if (ec || size == 0)
        return {};

    std::string   data(size, '\0');
    std::ifstream in(p, std::ios::binary);
    if (in.is_open())
    {
        in.read(data.data(), size);
    }

    if (auto nul = data.find_first_of('\0'); nul != std::string::npos)
        data.erase(nul);

    return data;
}

void asSCWorkspace::CompileScriptSources()
{
    for (auto& engine : engines)
    {
        if (!engine)
            continue;

        for (asUINT i = 0; i < engine->GetModuleCount(); i++)
        {
            auto module = engine->GetModuleByIndex(i);
            if (!module)
                continue;

            for (asUINT f = 0; f < module->GetFunctionCount(); f++)
            {
                auto func = module->GetFunctionByIndex(f);
                if (!func)
                    continue;

                const char* section = func->GetScriptSectionName();
                if (section && *section)
                    AddSection(section);
            }
        }
    }
}

void asSCWorkspace::CompileBreakpointPositions()
{
    potential_breakpoints.clear();

    auto addFunctionBreakpointLocations = [&](asIScriptFunction* func) {
        if (!func)
            return;

        const char* section = func->GetScriptSectionName();
        if (section && *section)
        {
            int line = func->FindNextLineWithCode(1);
            while (line > 0)
            {
                potential_breakpoints[section].insert(asIDBLineCol{line, 0});
                int next = func->FindNextLineWithCode(line + 1);
                if (next <= line)
                    break;
                line = next;
            }
        }
    };

    for (auto& engine : engines)
    {
        if (!engine)
            continue;

        for (asUINT i = 0; i < engine->GetModuleCount(); i++)
        {
            asIScriptModule* module = engine->GetModuleByIndex(i);
            if (!module)
                continue;

            for (asUINT f = 0; f < module->GetFunctionCount(); f++)
                addFunctionBreakpointLocations(module->GetFunctionByIndex(f));

            for (asUINT t = 0; t < module->GetObjectTypeCount(); t++)
            {
                asITypeInfo* type = module->GetObjectTypeByIndex(t);
                if (!type)
                    continue;

                for (asUINT m = 0; m < type->GetMethodCount(); m++)
                    addFunctionBreakpointLocations(type->GetMethodByIndex(m, false));

                for (asUINT m = 0; m < type->GetBehaviourCount(); m++)
                    addFunctionBreakpointLocations(type->GetBehaviourByIndex(m, nullptr));

                for (asUINT m = 0; m < type->GetFactoryCount(); m++)
                    addFunctionBreakpointLocations(type->GetFactoryByIndex(m));
            }
        }
    }
}
