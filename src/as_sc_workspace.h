#pragma once

#include "as_debugger.h"
#include <string>
#include <string_view>

class asSCWorkspace : public asIDBWorkspace
{
public:
    std::string base_path;

    asSCWorkspace(std::string_view basePath, std::initializer_list<asIScriptEngine*> engines);
    virtual ~asSCWorkspace() override = default;

    virtual std::string PathToSection(const std::string_view v) const override;
    virtual std::string SectionToPath(const std::string_view v) const override;

    virtual bool SectionIsVirtual(const std::string_view v) const override
    {
        return false;
    }

    virtual std::string SectionSource(const std::string_view v) const override;

    void CompileScriptSources();
    void CompileBreakpointPositions();
};
