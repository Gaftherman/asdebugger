// MIT Licensed
// see https://github.com/Paril/angelscript-debugger

#include "as_debugger.h"
#include <array>
#include <bitset>
#include <charconv>

void asIDBVariable::Evaluate()
{
    if (evaluated)
        return;
    // getters don't evaluate and are
    // just placeholders, but they need
    // a ref ID.
    else if (getter)
    {
        SetRefId();
        return;
    }

    auto var = ptr.lock();
    dbg.cache->GetEvaluator(var->address).Evaluate(var);
    evaluated = true;

    if (expandable)
        SetRefId();
}

void asIDBVariable::SetRefId()
{
    if (expandRefId.has_value())
        return;

    auto& refs = dbg.cache->variable_refs;

    int64_t next_id = refs.size() + 1;
    expandRefId     = next_id;
    refs.emplace(next_id, ptr);
}

void asIDBVariable::Expand()
{
    Evaluate();

    if (expanded)
        return;
    else if (!expandRefId)
        return;

    expanded = true;

    auto var = ptr.lock();

    if (!getter)
    {
        dbg.cache->GetEvaluator(var->address).Expand(var);
        return;
    }

    // getters are a bit special; we have to fetch the variable
    // that our getter is linked to, & store the result in stack memory.
    auto ctx = dbg.cache->ctx;

    dbg.internal_execution = true;
    ctx->PushState();

    ctx->Prepare(getter);
    ctx->SetObject(this->owner.lock()->address.ResolveAs<void>());
    ctx->Execute();

    var->namedProps.clear();
    var->indexedProps.clear();

    if (ctx->GetState() != asEXECUTION_FINISHED)
    {
        var->get_evaluated            = var->CreateChildVariable(var->identifier, {}, "");
        var->get_evaluated->value     = fmt::format("Exception thrown ({})", ctx->GetExceptionString());
        var->get_evaluated->evaluated = true;
    }
    else
    {
        asDWORD    returnFlags;
        int        typeId = getter->GetReturnTypeId(&returnFlags);
        asIDBValue returnValue(ctx->GetEngine(), ctx->GetAddressOfReturnValue(), typeId,
                               (returnFlags & asTM_INOUTREF) != 0);

        asIDBVariable::Ptr child =
            var->CreateChildVariable(var->identifier, {typeId, (returnFlags & asTM_CONST) != 0, nullptr},
                                     dbg.cache->GetTypeNameFromType({typeId, (asETypeModifiers)returnFlags}));
        child->stackValue      = std::move(returnValue);
        child->address.address = child->stackValue.GetPointer<void>(true);
    }

    ctx->PopState();
    dbg.internal_execution = false;
}

asIDBVariable::Ptr asIDBVariable::CreateChildVariable(asIDBVarName identifier, asIDBVarAddr address, std::string_view typeName)
{
    asIDBVariable::Ptr child = dbg.cache->CreateVariable();
    child->owner             = ptr;
    child->identifier        = identifier;
    child->address           = address;
    child->typeName          = typeName;
    if (identifier.name[0] == '[')
        indexedProps.push_back(child);
    else
        namedProps.insert(child);
    return child;
}

asIDBScope::asIDBScope(asUINT offset, asIDBDebugger& dbg, asIScriptFunction* function) :
    offset(offset),
    parameters(dbg.cache->CreateVariable()),
    locals(dbg.cache->CreateVariable()),
    registers(dbg.cache->CreateVariable())
{
    CalcLocals(dbg, function, parameters);
    CalcLocals(dbg, function, locals);
    CalcLocals(dbg, function, registers);
}

void asIDBScope::CalcLocals(asIDBDebugger& dbg, asIScriptFunction* function, asIDBVariable::Ptr& container)
{
    if (!function || offset == SCOPE_SYSTEM)
        return;

    auto&  cache     = *dbg.cache.get();
    auto   ctx       = cache.ctx;
    asUINT numParams = function->GetParamCount();
    asUINT numLocals = ctx->GetVarCount(offset);

    asUINT start = 0, end = 0;

    if (container == parameters)
        end = numParams;
    else
    {
        start = numParams;
        end   = numLocals;
    }

    if (container == locals)
    {
        if (auto thisPtr = ctx->GetThisPointer(offset))
        {
            int thisTypeId = ctx->GetThisTypeId(offset);

            asIDBTypeId typeKey{thisTypeId, asTM_NONE};

            const std::string_view viewType = cache.GetTypeNameFromType(typeKey);

            asIDBVarAddr idKey{thisTypeId, false, thisPtr};

            asIDBVariable::Ptr var = container->CreateChildVariable("this", idKey, viewType);

            this_ptr = var;
        }
    }

    for (asUINT n = start; n < end; n++)
    {
        const char*      name;
        int              typeId;
        asETypeModifiers modifiers;
        int              stackOffset;
        ctx->GetVar(n, offset, &name, &typeId, &modifiers, 0, &stackOffset);

        bool isTemporary = (container != parameters) && (!name || !*name || *name == '{' || *name == '}');

        if (!ctx->IsVarInScope(n, offset))
            continue;
        else if (isTemporary != (container == registers))
            continue;

        void* ptr = ctx->GetAddressOfVar(n, offset);

        asIDBTypeId typeKey{typeId, modifiers};

        std::string localName;
        if (name && *name && *name != '{' && *name != '}')
            localName = name;
        else
            localName = fmt::format("&{}", n);

        const std::string_view viewType = cache.GetTypeNameFromType(typeKey);

        asIDBVarAddr idKey{typeId, (modifiers & asTM_CONST) != 0, ptr};

        asIDBVariable::Ptr var = container->CreateChildVariable(std::move(localName), idKey, viewType);

        local_by_index.emplace(n, var);
    }

    container->evaluated = container->expanded = true;

    if (!container->namedProps.empty() ||
        !container->indexedProps.empty())
        container->SetRefId();
}

/*virtual*/ void asIDBCache::Refresh()
{
}

/*virtual*/ const std::string_view asIDBCache::GetTypeNameFromType(asIDBTypeId id)
{
    if (auto f = type_names.find(id); f != type_names.end())
        return f->second.c_str();

    auto        type    = ctx->GetEngine()->GetTypeInfoById(id.typeId);
    const char* rawName = "???";

    if (!type)
    {
        // a primitive
        switch (id.typeId & asTYPEID_MASK_SEQNBR)
        {
            case asTYPEID_BOOL: rawName = "bool"; break;
            case asTYPEID_INT8: rawName = "int8"; break;
            case asTYPEID_INT16: rawName = "int16"; break;
            case asTYPEID_INT32: rawName = "int32"; break;
            case asTYPEID_INT64: rawName = "int64"; break;
            case asTYPEID_UINT8: rawName = "uint8"; break;
            case asTYPEID_UINT16: rawName = "uint16"; break;
            case asTYPEID_UINT32: rawName = "uint32"; break;
            case asTYPEID_UINT64: rawName = "uint64"; break;
            case asTYPEID_FLOAT: rawName = "float"; break;
            case asTYPEID_DOUBLE: rawName = "double"; break;
            default: rawName = "???"; break;
        }
    }
    else
    {
        rawName = type->GetName();
    }

    std::string name = fmt::format("{}{}{}{}", (id.modifiers & asTM_CONST) ? "const " : "", rawName,
                                   (id.typeId & (asTYPEID_HANDLETOCONST | asTYPEID_OBJHANDLE)) ? "@" : "",
                                   ((id.modifiers & asTM_INOUTREF) == asTM_INOUTREF) ? "&" : ((id.modifiers & asTM_INOUTREF) == asTM_INREF) ? "&in" :
                                       ((id.modifiers & asTM_INOUTREF) == asTM_OUTREF)                                                      ? "&out" :
                                                                                                                                              "");

    return type_names.emplace(id, std::move(name)).first->second;
}

void* asIDBCache::ResolvePropertyAddress(const asIDBVarAddr& id, int propertyIndex, int offset, int compositeOffset, bool isCompositeIndirect)
{
    uint8_t* base = id.ResolveAs<uint8_t>();
    if (!base)
        return nullptr;

    if (id.typeId & asTYPEID_SCRIPTOBJECT)
    {
        asIScriptObject* obj = reinterpret_cast<asIScriptObject*>(base);
        return obj->GetAddressOfProperty(propertyIndex);
    }

    // indirect changes our ptr to
    // *(object + compositeOffset) + offset
    if (isCompositeIndirect)
    {
        uint8_t** indirectPtr = reinterpret_cast<uint8_t**>(base + compositeOffset);
        if (!indirectPtr || !*indirectPtr)
            return nullptr;

        return *indirectPtr + offset;
    }

    return base + offset + compositeOffset;
}

/*virtual*/ asIDBExpected<asIDBVariable::WeakPtr> asIDBCache::ResolveExpression(std::string_view   expr,
                                                                                std::optional<int> stack_index)
{
    // just in case your IDE sends `@ent` or `&ent` for a hover
    if (!expr.empty() && (expr[0] == '@' || (expr.size() >= 2 && expr[0] == '&' && !isdigit(expr[1]))))
        expr.remove_prefix(1);

    if (expr.empty())
        return asIDBExpected("empty string");

    CacheCallstack();

    // isolate the variable name first
    size_t           variable_end  = expr.find_first_of(".[", 0);
    std::string_view variable_name = expr.substr(0, variable_end);

    if (variable_name.empty())
        return asIDBExpected("bad expression");

    asIDBExpected<asIDBVariable::WeakPtr> variable;
    asIDBCallStackEntry*                  stack = nullptr;

    if (stack_index.has_value())
        stack = &call_stack[stack_index.value()];

    // if it starts with a & it has to be a local variable index
    if (stack && variable_name[0] == '&')
    {
        uint32_t offset;
        auto     result = std::from_chars(&variable_name.front(), &variable_name.front() + variable_name.size(), offset);

        if (result.ec != std::errc())
            return asIDBExpected("invalid numerical offset");

        // check bounds
        int m = ctx->GetVarCount(stack_index.value());

        if (m < 0)
            return asIDBExpected("bad stack index");

        if (offset >= (asUINT)m)
            return asIDBExpected("stack offset out of bounds");

        if (!ctx->IsVarInScope(offset, stack_index.value()))
            return asIDBExpected("variable out of scope");

        if (auto varit = stack->scope.local_by_index.find(offset); varit != stack->scope.local_by_index.end())
            variable = varit->second;
        else
            return asIDBExpected("missing local index");
    }
    // check this
    else if (stack && variable_name == "this")
    {
        if (stack->scope.this_ptr.expired())
            return asIDBExpected("not a method");

        variable = stack->scope.this_ptr;
    }
    else
    {
        struct asIDBNamespacedVar
        {
            asIDBVariable::WeakPtr var;
            std::string_view       name;
            std::string_view       ns;
        };

        std::vector<asIDBNamespacedVar> matches;
        std::string_view                variable_ns;

        if (auto ns_end = variable_name.find_last_of(':'); ns_end != std::string_view::npos)
        {
            variable_ns   = variable_name.substr(0, ns_end - 1);
            variable_name = variable_name.substr(ns_end + 1);
        }

        if (stack)
        {
            // not an offset; in order, check the following:
            // - local variables (in reverse order)
            // - function parameters
            // - class member properties (if appropriate)
            // - globals
            for (int i = ctx->GetVarCount(stack_index.value()) - 1; i >= 0; i--)
            {
                if (!ctx->IsVarInScope(i, stack_index.value()))
                    continue;

                const char*      name;
                int              typeId;
                asETypeModifiers modifiers;
                ctx->GetVar(i, stack_index.value(), &name, &typeId, &modifiers);

                if (variable_name != name)
                    continue;

                if (auto varit = stack->scope.local_by_index.find(i); varit != stack->scope.local_by_index.end())
                    matches.push_back({varit->second, name});

                break;
            }

            // check `this` parameters
            if (!stack->scope.this_ptr.expired())
            {
                auto var = stack->scope.this_ptr.lock();
                var->Expand();

                for (auto& param : var->namedProps)
                    if (variable_name == param->identifier.name)
                        matches.push_back({param, param->identifier.name});
                for (auto& param : var->indexedProps)
                    if (variable_name == param->identifier.name)
                        matches.push_back({param, param->identifier.name});
            }
        }

        // check globals
        CacheGlobals();

        for (auto& global : globals->namedProps)
            if (variable_name == global->identifier.name)
                matches.push_back({global, global->identifier.name, global->identifier.ns});
        for (auto& global : globals->indexedProps)
            if (variable_name == global->identifier.name)
                matches.push_back({global, global->identifier.name, global->identifier.ns});

        if (matches.size() == 1)
            variable = matches[0].var;
        // if we didn't specify a ns but had multiple
        // matches, return an error
        else if (variable_ns.empty())
            return asIDBExpected(matches.empty() ? "can't find variable" : "ambiguous variable name");
        else
        {
            for (auto& match : matches)
            {
                if (variable_ns == match.ns)
                {
                    variable = match.var;
                    break;
                }
            }
        }

        if (!variable)
            return asIDBExpected("can't find variable");
    }

    // variable_key should be non-null and with
    // a valid type ID here.
    return ResolveSubExpression(variable.value(), variable_end == std::string_view::npos ? std::string_view{} : expr.substr(variable_end));
}

/*virtual*/ asIDBExpected<asIDBVariable::WeakPtr> asIDBCache::ResolveSubExpression(asIDBVariable::WeakPtr var,
                                                                                   const std::string_view rest)
{
    // nothing left, so this is the result.
    if (rest.empty())
        return var;

    // make sure we're a type that supports properties
    auto varp = var.lock();

    varp->Evaluate();

    if (!varp->expandRefId)
        return asIDBExpected("invalid expression");

    varp->Expand();

    if (varp->namedProps.empty() && varp->indexedProps.empty())
        return asIDBExpected("no members");

    // check what kind of sub-evaluator to use
    size_t           eval_start = rest.find_first_of(".[", 1);
    std::string_view eval_name  = rest.substr(0, eval_start);

    if (eval_name[0] == '.')
        eval_name.remove_prefix(1);

    for (auto& child : varp->namedProps)
    {
        if (child->identifier.name == eval_name)
            return ResolveSubExpression(child, eval_start == std::string_view::npos ? std::string_view{} : rest.substr(eval_start));
    }

    for (auto& child : varp->indexedProps)
    {
        if (child->identifier.name == eval_name)
            return ResolveSubExpression(child, eval_start == std::string_view::npos ? std::string_view{} : rest.substr(eval_start));
    }

    return asIDBExpected("can't resolve sub-expression");
}

/*virtual*/ void asIDBCache::CacheCallstack()
{
    if (!ctx || !call_stack.empty())
        return;

    if (auto sysfunc = ctx->GetSystemFunction())
        call_stack.emplace_back(asIDBCallStackEntry{dbg.frame_offset++, sysfunc->GetDeclaration(true, false, true),
                                                    "", 0, 0, sysfunc,
                                                    asIDBScope(SCOPE_SYSTEM, dbg, sysfunc)});

    for (asUINT n = 0; n < ctx->GetCallstackSize(); n++)
    {
        asIScriptFunction* func    = nullptr;
        int                column  = 0;
        const char*        section = "";
        int                row     = 0;

        // FIXME: check this, because this will skip GetFunction(n).
        // I think this is correct though...?
        if (n == 0 && ctx->GetState() == asEXECUTION_EXCEPTION)
        {
            func = ctx->GetExceptionFunction();
            if (func)
                row = ctx->GetExceptionLineNumber(&column, &section);
        }
        else
        {
            func = ctx->GetFunction(n);
            if (func)
                row = ctx->GetLineNumber(n, &column, &section);
        }

        std::string decl;

        if (func)
            decl = func->GetDeclaration(true, false, true);
        else
            decl = "???"; // FIXME: why does this happen?

        call_stack.push_back(
            asIDBCallStackEntry{dbg.frame_offset++, std::move(decl), section, row, column, func,
                                asIDBScope(func && func->GetFuncType() == asFUNC_SYSTEM ? SCOPE_SYSTEM : n, dbg, func)});
    }
}

// restore data from the given cache that is
// being replaced by this one.
/*virtual*/ void asIDBCache::Restore(asIDBCache& cache)
{
}

/*virtual*/ void asIDBCache::CacheGlobalsForModule(asIScriptModule* module)
{
    if (!module)
        return;

    auto it = module_globals.find(module);
    if (it != module_globals.end() && it->second && it->second->expanded)
        return;

    asIDBVariable::Ptr modVar = CreateVariable();
    for (asUINT n = 0; n < module->GetGlobalVarCount(); n++)
    {
        const char* name      = nullptr;
        const char* nameSpace = nullptr;
        int         typeId    = 0;
        bool        isConst   = false;

        module->GetGlobalVar(n, &name, &nameSpace, &typeId, &isConst);
        void* ptr = module->GetAddressOfGlobalVar(n);

        asIDBTypeId            typeKey{typeId, isConst ? asTM_CONST : asTM_NONE};
        const std::string_view viewType = GetTypeNameFromType(typeKey);

        asIDBVarAddr idKey{typeId, isConst, ptr};

        modVar->CreateChildVariable(asIDBVarName((nameSpace && nameSpace[0]) ? nameSpace : "", name ? name : ""), idKey, viewType);
    }
    modVar->evaluated = modVar->expanded = true;
    if (!modVar->namedProps.empty() || !modVar->indexedProps.empty())
        modVar->SetRefId();

    module_globals[module] = modVar;
}

/*virtual*/ void asIDBCache::CacheEngineGlobals()
{
    if (!ctx || (engine_globals && engine_globals->expanded))
        return;

    engine_globals          = CreateVariable();
    asIScriptEngine* engine = ctx->GetEngine();
    if (engine)
    {
        for (asUINT n = 0; n < engine->GetGlobalPropertyCount(); n++)
        {
            const char* name      = nullptr;
            const char* nameSpace = nullptr;
            int         typeId    = 0;
            void*       ptr       = nullptr;
            bool        isConst   = false;

            engine->GetGlobalPropertyByIndex(n, &name, &nameSpace, &typeId, &isConst, nullptr, &ptr);

            asIDBTypeId            typeKey{typeId, isConst ? asTM_CONST : asTM_NONE};
            const std::string_view viewType = GetTypeNameFromType(typeKey);

            asIDBVarAddr idKey{typeId, isConst, ptr};

            std::string localName = (nameSpace && nameSpace[0]) ? fmt::format("{}::{}", nameSpace, name ? name : "") : (name ? name : "");

            engine_globals->CreateChildVariable(std::move(localName), idKey, viewType);
        }
    }
    engine_globals->evaluated = engine_globals->expanded = true;
    if (!engine_globals->namedProps.empty() || !engine_globals->indexedProps.empty())
        engine_globals->SetRefId();
}

/*virtual*/ asIDBVariable::Ptr asIDBCache::GetModuleGlobals(asIScriptModule* module)
{
    if (!module)
        return nullptr;
    auto it = module_globals.find(module);
    if (it != module_globals.end())
        return it->second;
    return nullptr;
}

/*virtual*/ asIDBVariable::Ptr asIDBCache::GetEngineGlobals()
{
    return engine_globals;
}

/*virtual*/ asIDBVariable::Ptr asIDBCache::GetOtherModules(asIScriptModule* activeModule)
{
    if (!ctx)
        return nullptr;

    asIScriptEngine* engine = ctx->GetEngine();
    if (!engine)
        return nullptr;

    asUINT modCount = engine->GetModuleCount();
    if (modCount <= 1)
        return nullptr;

    asIDBVariable::Ptr others = CreateVariable();
    for (asUINT m = 0; m < modCount; m++)
    {
        asIScriptModule* mod = engine->GetModuleByIndex(m);
        if (!mod || mod == activeModule)
            continue;

        CacheGlobalsForModule(mod);
        auto modVars = module_globals[mod];
        if (modVars && (!modVars->namedProps.empty() || !modVars->indexedProps.empty()))
        {
            const char* modName     = mod->GetName();
            std::string displayName = (modName && *modName) ? modName : fmt::format("Module_{}", m);
            auto        child       = others->CreateChildVariable(displayName, {}, "module");
            child->expandRefId      = modVars->expandRefId;
            child->namedProps       = modVars->namedProps;
            child->indexedProps     = modVars->indexedProps;
            child->evaluated = child->expanded = true;
        }
    }
    others->evaluated = others->expanded = true;
    if (!others->namedProps.empty())
        others->SetRefId();

    return others;
}

/*virtual*/ void asIDBCache::CacheGlobals()
{
    if (!ctx)
        return;

    if (!globals)
        globals = CreateVariable();

    if (globals->expanded)
        return;

    auto func = ctx->GetFunction(0);
    auto main = func ? func->GetModule() : nullptr;

    if (main)
    {
        CacheGlobalsForModule(main);
        for (asUINT n = 0; n < main->GetGlobalVarCount(); n++)
        {
            const char* name;
            const char* nameSpace;
            int         typeId;
            void*       ptr;
            bool        isConst;

            main->GetGlobalVar(n, &name, &nameSpace, &typeId, &isConst);
            ptr = main->GetAddressOfGlobalVar(n);

            asIDBTypeId            typeKey{typeId, isConst ? asTM_CONST : asTM_NONE};
            const std::string_view viewType = GetTypeNameFromType(typeKey);

            asIDBVarAddr idKey{typeId, isConst, ptr};

            globals->CreateChildVariable(asIDBVarName((nameSpace && nameSpace[0]) ? nameSpace : "", name), idKey, viewType);
        }
    }

    CacheEngineGlobals();
    if (auto engine = ctx->GetEngine())
    {
        for (asUINT n = 0; n < engine->GetGlobalPropertyCount(); n++)
        {
            const char* name;
            const char* nameSpace;
            int         typeId;
            void*       ptr;
            bool        isConst;

            engine->GetGlobalPropertyByIndex(n, &name, &nameSpace, &typeId, &isConst, nullptr, &ptr);

            asIDBTypeId            typeKey{typeId, isConst ? asTM_CONST : asTM_NONE};
            const std::string_view viewType = GetTypeNameFromType(typeKey);

            asIDBVarAddr idKey{typeId, isConst, ptr};

            std::string localName = (nameSpace && nameSpace[0]) ? fmt::format("{}::{}", nameSpace, name) : name;

            globals->CreateChildVariable(std::move(localName), idKey, viewType);
        }
    }

    globals->evaluated = globals->expanded = true;

    if (!globals->namedProps.empty() ||
        !globals->indexedProps.empty())
        globals->SetRefId();
}

#ifdef _WIN32
#    include <windows.h>

struct DictEntryRaw
{
    const CString* keyObj;
    void*          node;
};

static bool SafeGetDictSize(CScriptDictionary* dict, unsigned int& out_count)
{
    __try
    {
        if (!dict || !ASEXT_CScriptDictionary_GetSize)
            return false;
        out_count = ASEXT_CScriptDictionary_GetSize(dict);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static size_t SafeIterateDict(CScriptDictionary* dict, DictEntryRaw* out_entries, size_t max_entries)
{
    __try
    {
        if (!dict || !ASEXT_CScriptDictionary_begin || !ASEXT_CScriptDictionary_end ||
            !ASEXT_CScriptDictionary_CIterator_operator_NE || !ASEXT_CScriptDictionary_CIterator_operator_PP ||
            !ASEXT_CScriptDictionary_CIterator_GetKey)
            return 0;

        CScriptDictionary_CIterator it{}, itend{};
        ASEXT_CScriptDictionary_begin(dict, &it);
        ASEXT_CScriptDictionary_end(dict, &itend);

        size_t count = 0;
        while (ASEXT_CScriptDictionary_CIterator_operator_NE(&it, &itend) && count < max_entries)
        {
            out_entries[count].keyObj = ASEXT_CScriptDictionary_CIterator_GetKey(&it);
            out_entries[count].node   = it.cur;
            count++;
            ASEXT_CScriptDictionary_CIterator_operator_PP(&it);
        }
        return count;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

static bool SafeReadCString(const void* ptr, char* out_buf, size_t buf_size, size_t& out_len, bool& out_truncated)
{
    __try
    {
        if (!ptr)
            return false;
        const CString* cstr = reinterpret_cast<const CString*>(ptr);
        size_t         len  = cstr->m_len;
        if (len > 10000000)
            return false;

        if (len == 0)
        {
            out_len       = 0;
            out_truncated = false;
            if (buf_size > 0)
                out_buf[0] = '\0';
            return true;
        }

        const char* p = cstr->m_ptr;
        if (!p)
            return false;

        out_len        = len;
        size_t to_copy = (len >= buf_size) ? (buf_size - 1) : len;
        out_truncated  = (len >= buf_size);

        for (size_t i = 0; i < to_copy; i++)
        {
            out_buf[i] = p[i];
        }
        out_buf[to_copy] = '\0';
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool SafeReadStdString(const void* ptr, char* out_buf, size_t buf_size, size_t& out_len, bool& out_truncated)
{
    __try
    {
        if (!ptr)
            return false;
        const std_string* strPtr = reinterpret_cast<const std_string*>(ptr);
        size_t            len    = strPtr->len();
        if (len > 10000000)
            return false;

        if (len == 0)
        {
            out_len       = 0;
            out_truncated = false;
            if (buf_size > 0)
                out_buf[0] = '\0';
            return true;
        }

        const char* c_str = const_cast<std_string*>(strPtr)->c_str();
        if (!c_str)
            return false;

        out_len        = len;
        size_t to_copy = (len >= buf_size) ? (buf_size - 1) : len;
        out_truncated  = (len >= buf_size);

        for (size_t i = 0; i < to_copy; i++)
        {
            out_buf[i] = c_str[i];
        }
        out_buf[to_copy] = '\0';
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool SafeInspectScriptArray(const void* ptr, size_t& out_count, void*& out_data, int& out_elemTypeId, int& out_elemSize)
{
    __try
    {
        if (!ptr)
            return false;
        const CScriptArray* arr = reinterpret_cast<const CScriptArray*>(ptr);
        CScriptArrayBuffer* buf = arr->m_buffer;
        out_elemTypeId          = arr->m_ElementTypeId;
        out_elemSize            = arr->m_unk5; // offset 20 is elementSize in Sven Co-op CScriptArray

        if (!buf)
        {
            out_count = 0;
            out_data  = nullptr;
            return true;
        }

        int count = buf->m_size;
        if (count < 0 || count > 10000000)
            return false;

        out_count = static_cast<size_t>(count);
        out_data  = buf->data();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
static void* SafeReadPointer(void* ptr)
{
    __try
    {
        return ptr ? *reinterpret_cast<void**>(ptr) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}
#else
static void* SafeReadPointer(void* ptr)
{
    return ptr ? *reinterpret_cast<void**>(ptr) : nullptr;
}

static bool SafeReadCString(const void* ptr, char* out_buf, size_t buf_size, size_t& out_len, bool& out_truncated)
{
    if (!ptr)
        return false;
    const CString* cstr = reinterpret_cast<const CString*>(ptr);
    size_t         len  = cstr->m_len;
    if (len > 10000000)
        return false;
    if (len == 0)
    {
        out_len       = 0;
        out_truncated = false;
        if (buf_size > 0)
            out_buf[0] = '\0';
        return true;
    }
    const char* p = cstr->m_ptr;
    if (!p)
        return false;
    out_len        = len;
    size_t to_copy = (len >= buf_size) ? (buf_size - 1) : len;
    out_truncated  = (len >= buf_size);
    memcpy(out_buf, p, to_copy);
    out_buf[to_copy] = '\0';
    return true;
}

static bool SafeReadStdString(const void* ptr, char* out_buf, size_t buf_size, size_t& out_len, bool& out_truncated)
{
    if (!ptr)
        return false;
    const std_string* strPtr = reinterpret_cast<const std_string*>(ptr);
    size_t            len    = strPtr->len();
    if (len > 10000000)
        return false;
    if (len == 0)
    {
        out_len       = 0;
        out_truncated = false;
        if (buf_size > 0)
            out_buf[0] = '\0';
        return true;
    }
    const char* c_str = const_cast<std_string*>(strPtr)->c_str();
    if (!c_str)
        return false;
    out_len        = len;
    size_t to_copy = (len >= buf_size) ? (buf_size - 1) : len;
    out_truncated  = (len >= buf_size);
    memcpy(out_buf, c_str, to_copy);
    out_buf[to_copy] = '\0';
    return true;
}

static bool SafeInspectScriptArray(const void* ptr, size_t& out_count, void*& out_data, int& out_elemTypeId, int& out_elemSize)
{
    if (!ptr)
        return false;
    const CScriptArray* arr = reinterpret_cast<const CScriptArray*>(ptr);
    CScriptArrayBuffer* buf = arr->m_buffer;
    out_elemTypeId          = arr->m_ElementTypeId;
    out_elemSize            = arr->m_unk5; // offset 20 is elementSize in Sven Co-op CScriptArray

    if (!buf)
    {
        out_count = 0;
        out_data  = nullptr;
        return true;
    }

    int count = buf->m_size;
    if (count < 0 || count > 10000000)
        return false;

    out_count = static_cast<size_t>(count);
    out_data  = buf->data();
    return true;
}

static bool SafeGetDictSize(CScriptDictionary* dict, unsigned int& out_count)
{
    if (!dict || !ASEXT_CScriptDictionary_GetSize)
        return false;
    out_count = ASEXT_CScriptDictionary_GetSize(dict);
    return true;
}

static size_t SafeIterateDict(CScriptDictionary* dict, DictEntryRaw* out_entries, size_t max_entries)
{
    if (!dict || !ASEXT_CScriptDictionary_begin || !ASEXT_CScriptDictionary_end ||
        !ASEXT_CScriptDictionary_CIterator_operator_NE || !ASEXT_CScriptDictionary_CIterator_operator_PP ||
        !ASEXT_CScriptDictionary_CIterator_GetKey)
        return 0;

    CScriptDictionary_CIterator it{}, itend{};
    ASEXT_CScriptDictionary_begin(dict, &it);
    ASEXT_CScriptDictionary_end(dict, &itend);

    size_t count = 0;
    while (ASEXT_CScriptDictionary_CIterator_operator_NE(&it, &itend) && count < max_entries)
    {
        out_entries[count].keyObj = ASEXT_CScriptDictionary_CIterator_GetKey(&it);
        out_entries[count].node   = it.cur;
        count++;
        ASEXT_CScriptDictionary_CIterator_operator_PP(&it);
    }
    return count;
}
#endif

class asIDBStringTypeEvaluator : public asIDBTypeEvaluator
{
public:
    virtual void Evaluate(asIDBVariable::Ptr var) const override
    {
        if (!var || !var->address.address)
        {
            var->value = "(null)";
            return;
        }

        void* rawPtr = var->address.ResolveAs<void>();
        if (!rawPtr)
        {
            var->value = "(null)";
            return;
        }

        auto   ctx      = var->dbg.cache ? var->dbg.cache->ctx : nullptr;
        auto   engine   = ctx ? ctx->GetEngine() : nullptr;
        auto   type     = engine ? engine->GetTypeInfoById(var->address.typeId) : nullptr;
        size_t typeSize = type ? type->GetSize() : 0;

        char   buf[257];
        size_t len       = 0;
        bool   truncated = false;
        bool   ok        = false;

        if (typeSize == 36 || typeSize == sizeof(CString))
        {
            ok = SafeReadCString(rawPtr, buf, sizeof(buf), len, truncated);
            if (!ok)
                ok = SafeReadStdString(rawPtr, buf, sizeof(buf), len, truncated);
        }
        else if (typeSize == 24 || typeSize == sizeof(std_string))
        {
            ok = SafeReadStdString(rawPtr, buf, sizeof(buf), len, truncated);
            if (!ok)
                ok = SafeReadCString(rawPtr, buf, sizeof(buf), len, truncated);
        }
        else
        {
            ok = SafeReadCString(rawPtr, buf, sizeof(buf), len, truncated);
            if (!ok)
                ok = SafeReadStdString(rawPtr, buf, sizeof(buf), len, truncated);
        }

        if (!ok)
        {
            var->value = "\"(invalid string)\"";
            return;
        }

        if (truncated || len > 256)
        {
            var->value = fmt::format("\"{}...\" (len={})", std::string_view(buf, std::min<size_t>(len, sizeof(buf) - 1)), len);
        }
        else
        {
            var->value = fmt::format("\"{}\"", std::string_view(buf, len));
        }
    }
};

class asIDBNullTypeEvaluator : public asIDBTypeEvaluator
{
public:
    virtual void Evaluate(asIDBVariable::Ptr var) const override
    {
        var->value = "(null)";
    }
};

class asIDBUninitTypeEvaluator : public asIDBTypeEvaluator
{
public:
    virtual void Evaluate(asIDBVariable::Ptr var) const override
    {
        var->value = "(uninit)";
    }
};

class asIDBEnumTypeEvaluator : public asIDBTypeEvaluator
{
public:
    virtual void Evaluate(asIDBVariable::Ptr var) const override
    {
        if (!var || !var->address.address)
        {
            var->value = "(null)";
            return;
        }

        auto& dbg  = var->dbg;
        auto  ctx  = dbg.cache ? dbg.cache->ctx : nullptr;
        auto  type = ctx ? ctx->GetEngine()->GetTypeInfoById(var->address.typeId) : nullptr;
        if (!type)
        {
            var->value = "(uninit)";
            return;
        }

        void* rawPtr = var->address.ResolveAs<void>();
        if (!rawPtr)
        {
            var->value = "(null)";
            return;
        }

        asINT64 v  = 0;
        size_t  sz = type->GetSize();
        if (sz == 1)
            v = *reinterpret_cast<const int8_t*>(rawPtr);
        else if (sz == 2)
            v = *reinterpret_cast<const int16_t*>(rawPtr);
        else if (sz == 8)
            v = *reinterpret_cast<const int64_t*>(rawPtr);
        else
            v = *reinterpret_cast<const int32_t*>(rawPtr);

        for (asUINT e = 0; e < type->GetEnumValueCount(); e++)
        {
#if ANGELSCRIPT_VERSION >= 23800
            asINT64     ov   = 0;
            const char* name = type->GetEnumValueByIndex(e, &ov);
#else
            int         ov_int = 0;
            const char* name   = type->GetEnumValueByIndex(e, &ov_int);
            asINT64     ov     = ov_int;
#endif

            if (ov == v)
            {
                var->value = fmt::format("{} ({})", name ? name : "???", v);
                return;
            }
        }

        std::bitset<32> bits(static_cast<uint32_t>(v));

        if (bits.count() <= 1)
        {
            var->value = fmt::format("{}", v);
            return;
        }

        var->value      = fmt::format("{} bits ({})", bits.count(), v);
        var->expandable = true;
    }

    virtual void Expand(asIDBVariable::Ptr var) const override
    {
        if (!var || !var->address.address)
            return;

        auto& dbg   = var->dbg;
        auto& cache = *dbg.cache;
        auto  ctx   = cache.ctx;
        auto  type  = ctx ? ctx->GetEngine()->GetTypeInfoById(var->address.typeId) : nullptr;
        if (!type)
            return;

        void* rawPtr = var->address.ResolveAs<void>();
        if (!rawPtr)
            return;

        asINT64 v  = 0;
        size_t  sz = type->GetSize();
        if (sz == 1)
            v = *reinterpret_cast<const int8_t*>(rawPtr);
        else if (sz == 2)
            v = *reinterpret_cast<const int16_t*>(rawPtr);
        else if (sz == 8)
            v = *reinterpret_cast<const int64_t*>(rawPtr);
        else
            v = *reinterpret_cast<const int32_t*>(rawPtr);

        {
            auto child       = var->CreateChildVariable("value", {}, "");
            child->value     = fmt::format("{}", v);
            child->evaluated = true;
        }

        // find bit names
        asINT64                                 ov = 0;
        std::array<const char*, sizeof(ov) * 8> bit_names{};

        for (asUINT e = 0; e < type->GetEnumValueCount(); e++)
        {
#if ANGELSCRIPT_VERSION >= 23800
            const char* name = type->GetEnumValueByIndex(e, &ov);
#else
            int         ov_int = 0;
            const char* name   = type->GetEnumValueByIndex(e, &ov_int);
            ov                 = ov_int;
#endif
            std::bitset<sizeof(ov) * 8> obits(ov);

            // skip masks
            if (obits.count() != 1)
                continue;

            if (ov & v)
            {
                int p = 0;

                while (ov && !(ov & 1))
                {
                    ov >>= 1;
                    p++;
                }

                // only take the first name, just incase
                // there's later overrides
                if (p <= (obits.size() - 1) && !bit_names[p])
                    bit_names[p] = name;
            }
        }

        // display bits
        for (asQWORD e = 0; e < bit_names.size(); e++)
        {
            if (v & (1ull << e))
            {
                std::string bitEntry;

                if (bit_names[e])
                    bitEntry = bit_names[e];
                else
                    bitEntry = fmt::format("{}", 1 << e);

                auto child       = var->CreateChildVariable(fmt::format("[{:{}}]", e, type->GetSize() == 1 ? 1 : 2), {}, "");
                child->value     = std::move(bitEntry);
                child->evaluated = true;
            }
        }
    }
};

class asIDBFuncDefTypeEvaluator : public asIDBTypeEvaluator
{
public:
    virtual void Evaluate(asIDBVariable::Ptr var) const override
    {
        asIScriptFunction* ptr = var->address.ResolveAs<asIScriptFunction>();
        auto&              dbg = var->dbg;
        var->value             = ptr->GetName();
    }
};

/*virtual*/ void asIDBObjectTypeEvaluator::Evaluate(asIDBVariable::Ptr var) const /*override*/
{
    auto& dbg   = var->dbg;
    auto& cache = *dbg.cache;
    auto  ctx   = cache.ctx;
    auto  type  = ctx->GetEngine()->GetTypeInfoById(var->address.typeId);

    if (!type)
    {
        if (var->value.empty())
            var->value = fmt::format("{{{}}}", var->typeName);
        var->expandable = false;
        return;
    }

    var->expandable = CanExpand(var);

    void* objPtr = var->address.ResolveAs<void>();
    if (ctx->GetState() != asEXECUTION_EXCEPTION && objPtr != nullptr)
    {
        asIDBObjectIteratorHelper it(type, objPtr);

        if (!it)
        {
            if (!it.error.empty())
            {
                var->value = std::string(it.error);
                return;
            }

            if (var->value.empty())
                var->value = fmt::format("{{{}}}", var->typeName);
        }
        else
        {
            dbg.internal_execution = true;
            size_t numElements     = it.CalculateLength(ctx);
            dbg.internal_execution = false;

            if (var->value.empty())
                var->value = fmt::format("{} elements", numElements);

            if (numElements)
                var->expandable = true;
        }
    }
}

/*virtual*/ void asIDBObjectTypeEvaluator::Expand(asIDBVariable::Ptr var) const /*override*/
{
    QueryVariableProperties(var);

    QueryVariableGetters(var);

    QueryVariableForEach(var);
}

// convenience function that queries the properties of the given
// address (and object, if set) of the given type.
void asIDBObjectTypeEvaluator::QueryVariableProperties(asIDBVariable::Ptr var) const
{
    auto& dbg   = var->dbg;
    auto& cache = *dbg.cache;
    auto  type  = cache.ctx->GetEngine()->GetTypeInfoById(var->address.typeId);

    if (!type || var->address.ResolveAs<void>() == nullptr)
        return;

    for (asUINT n = 0; n < type->GetPropertyCount(); n++)
    {
        const char* name;
        int         propTypeId;
        void*       propAddr = nullptr;
        int         offset;
        int         compositeOffset;
        bool        isCompositeIndirect;
        bool        isReadOnly;

#if ANGELSCRIPT_VERSION >= 23800
        type->GetProperty(n, &name, &propTypeId, 0, 0, &offset, 0, 0, &compositeOffset, &isCompositeIndirect,
                          &isReadOnly);
#else
        type->GetProperty(n, &name, &propTypeId, 0, 0, &offset, 0, 0, &compositeOffset, &isCompositeIndirect);
        isReadOnly = false;
#endif

        propAddr = cache.ResolvePropertyAddress(var->address, n, offset, compositeOffset, isCompositeIndirect);

        asIDBVarAddr propId{propTypeId, isReadOnly, propAddr};

        // TODO: variables that overlap memory space will
        // get culled by this. this helps in the case of
        // vec3_t::x and vec3_t::pitch for instance, but
        // causes some confusion for edict_t::number and
        // edict_t::s::number, where `s` is now just an empty
        // struct. it'd be ideal if, in this case, it prefers
        // the deeper nested ones. not sure how we'd express that
        // with the limited context we have, though.

        // TODO 2.0: this causes an issue with Watch variables
        // because of the way dereferencing works. For now, it
        // will add duplicates, and the old var state cache is gone.
        var->CreateChildVariable(name, propId,
                                 cache.GetTypeNameFromType({propTypeId, isReadOnly ? asTM_CONST : asTM_NONE}));
    }
}

// convenience function that queries for getter property functions.
void asIDBObjectTypeEvaluator::QueryVariableGetters(asIDBVariable::Ptr var) const
{
    auto& dbg   = var->dbg;
    auto& cache = *dbg.cache;
    auto  type  = cache.ctx->GetEngine()->GetTypeInfoById(var->address.typeId);

    if (!type || var->address.ResolveAs<void>() == nullptr)
        return;

    for (asUINT n = 0; n < type->GetMethodCount(); n++)
    {
        asIScriptFunction* function = type->GetMethodByIndex(n, true);

        if (!IsCompatibleGetter(function))
            continue;

        auto child    = var->CreateChildVariable(std::string(std::string_view(function->GetName()).substr(4)), {},
                                                 cache.GetTypeNameFromType({function->GetReturnTypeId(), asTM_NONE}));
        child->getter = function;
        child->Evaluate();
    }
}

bool asIDBObjectTypeEvaluator::CanExpand(asIDBVariable::Ptr var) const
{
    auto& dbg   = var->dbg;
    auto& cache = *dbg.cache;
    auto  type  = cache.ctx->GetEngine()->GetTypeInfoById(var->address.typeId);

    if (!type || var->address.ResolveAs<void>() == nullptr)
        return false;

    if (type->GetPropertyCount())
        return true;

    for (asUINT n = 0; n < type->GetMethodCount(); n++)
    {
        asIScriptFunction* function = type->GetMethodByIndex(n, true);

        if (IsCompatibleGetter(function))
            return true;
    }

    return false;
}

bool asIDBObjectTypeEvaluator::IsCompatibleGetter(asIScriptFunction* function) const
{
    return function->IsReadOnly() && function->IsProperty() && function->GetParamCount() == 0;
}

// convenience function that iterates the opFor* of the given
// address (and object, if set) of the given type. If positive,
// a specific index will be used.
void asIDBObjectTypeEvaluator::QueryVariableForEach(asIDBVariable::Ptr var, int index) const
{
    auto& dbg   = var->dbg;
    auto& cache = *dbg.cache;
    auto  ctx   = cache.ctx;

    if (ctx->GetState() == asEXECUTION_EXCEPTION)
        return;

    auto  type   = ctx->GetEngine()->GetTypeInfoById(var->address.typeId);
    void* objPtr = var->address.ResolveAs<void>();

    if (!type || !objPtr)
        return;

    dbg.internal_execution = true;
    asIDBObjectIteratorHelper it(type, objPtr);

    if (!it)
    {
        dbg.internal_execution = false;
        return;
    }

    ctx->PushState();

    auto itValue      = it.Begin(ctx);
    int  elementId    = 0;
    bool multiElement = index == -1 && it.opForValues.size() > 1;

    while (true)
    {
        if (it.End(ctx, itValue))
            break;

        asIDBVariable::Ptr indexVar;

        // if we're a multi-element, the root is fake
        // and just exists to store the element id.
        if (multiElement)
        {
            indexVar           = var->CreateChildVariable(fmt::format("[{}]", elementId), {},
                                                          "" // FIXME: could show types as tuple?
            );
            indexVar->expanded = indexVar->evaluated = true;
        }

        for (int offset = (index == -1 ? 0 : index), visibleOffset = 0;
             offset < (index == -1 ? it.opForValues.size() : index + 1); offset++, visibleOffset++)
        {
            it.Value(ctx, itValue, offset);

            asDWORD    returnFlags;
            int        typeId = it.opForValues[offset]->GetReturnTypeId(&returnFlags);
            asIDBValue returnValue(ctx->GetEngine(), ctx->GetAddressOfReturnValue(), typeId,
                                   (returnFlags & asTM_INOUTREF) != 0);

            auto child =
                (multiElement ? indexVar : var)
                    ->CreateChildVariable(fmt::format("[{}]", multiElement ? visibleOffset : elementId),
                                          {typeId, (returnFlags & asTM_CONST) != 0, nullptr},
                                          dbg.cache->GetTypeNameFromType({typeId, (asETypeModifiers)returnFlags}));
            child->stackValue      = std::move(returnValue);
            child->address.address = child->stackValue.GetPointer<void>(true);
        }

        itValue = it.Next(ctx, itValue);

        elementId++;
    }

    ctx->PopState();
    cache.dbg.internal_execution = false;
}

void asIDBArrayTypeEvaluator::Evaluate(asIDBVariable::Ptr var) const
{
    if (!var || !var->address.address)
    {
        var->value      = "(null)";
        var->expandable = false;
        return;
    }

    void* objPtr = var->address.ResolveAs<void>();
    if (!objPtr)
    {
        var->value      = "(null)";
        var->expandable = false;
        return;
    }

    size_t count      = 0;
    void*  data       = nullptr;
    int    elemTypeId = 0;
    int    elemSize   = 0;

    if (!SafeInspectScriptArray(objPtr, count, data, elemTypeId, elemSize))
    {
        var->value      = "\"(invalid array)\"";
        var->expandable = false;
        return;
    }

    var->value      = fmt::format("{} elements", count);
    var->expandable = (count > 0);
}

void asIDBArrayTypeEvaluator::Expand(asIDBVariable::Ptr var) const
{
    if (!var || !var->address.address)
        return;

    void* objPtr = var->address.ResolveAs<void>();
    if (!objPtr)
        return;

    size_t count      = 0;
    void*  data       = nullptr;
    int    elemTypeId = 0;
    int    elemSize   = 0;

    if (!SafeInspectScriptArray(objPtr, count, data, elemTypeId, elemSize))
        return;

    if (count == 0 || !data)
        return;

    auto ctx    = var->dbg.cache ? var->dbg.cache->ctx : nullptr;
    auto engine = ctx ? ctx->GetEngine() : nullptr;

    if (engine && elemTypeId <= 0)
    {
        auto type = engine->GetTypeInfoById(var->address.typeId);
        if (type)
            elemTypeId = type->GetSubTypeId();
    }

    std::string_view elemTypeName = "";
    if (var->dbg.cache)
        elemTypeName = var->dbg.cache->GetTypeNameFromType({elemTypeId, asTM_NONE});

    size_t elemStride = sizeof(void*);
    if (elemTypeId & asTYPEID_MASK_OBJECT)
    {
        // In Sven Co-op CScriptArray, all object elements (both value objects like CString
        // and handles like CBaseEntity@) are stored as 4-byte pointers in the array buffer.
        elemStride = sizeof(void*);
    }
    else if (elemSize > 0 && elemSize <= 64)
    {
        elemStride = static_cast<size_t>(elemSize);
    }
    else if (engine)
    {
        int primSize = engine->GetSizeOfPrimitiveType(elemTypeId);
        elemStride = primSize > 0 ? static_cast<size_t>(primSize) : sizeof(void*);
    }

    constexpr size_t MAX_EXPAND_ELEMENTS = 256;
    size_t           limit               = std::min<size_t>(count, MAX_EXPAND_ELEMENTS);

    for (size_t i = 0; i < limit; i++)
    {
        void* slot     = static_cast<char*>(data) + i * elemStride;
        void* elemAddr = slot;

        // In CScriptArray, value objects (e.g. string) store pointers to the allocated objects
        // in each buffer slot. We must dereference the slot so the evaluator receives the
        // direct pointer to the object (CString*) instead of a pointer to the slot (CString**).
        if ((elemTypeId & asTYPEID_MASK_OBJECT) && !(elemTypeId & (asTYPEID_HANDLETOCONST | asTYPEID_OBJHANDLE)))
        {
            elemAddr = SafeReadPointer(slot);
        }

        var->CreateChildVariable(fmt::format("[{}]", i), asIDBVarAddr{elemTypeId, false, elemAddr}, elemTypeName);
    }

    if (count > MAX_EXPAND_ELEMENTS)
    {
        auto notice = var->CreateChildVariable(
            fmt::format("[... {} elements omitted to prevent engine freeze ...]", count - MAX_EXPAND_ELEMENTS),
            asIDBVarAddr{0, false, nullptr},
            "notice");
        notice->value      = fmt::format("{} more elements", count - MAX_EXPAND_ELEMENTS);
        notice->evaluated  = true;
        notice->expandable = false;
    }
}

void asIDBDictionaryTypeEvaluator::Evaluate(asIDBVariable::Ptr var) const
{
    if (!var || !var->address.address)
    {
        var->value      = "(null)";
        var->expandable = false;
        return;
    }

    void* objPtr = var->address.ResolveAs<void>();
    if (!objPtr)
    {
        var->value      = "(null)";
        var->expandable = false;
        return;
    }

    unsigned int count = 0;
    if (!SafeGetDictSize(reinterpret_cast<CScriptDictionary*>(objPtr), count))
    {
        var->value      = "\"(invalid dictionary)\"";
        var->expandable = false;
        return;
    }

    var->value      = fmt::format("{} entries", count);
    var->expandable = (count > 0);
}

void asIDBDictionaryTypeEvaluator::Expand(asIDBVariable::Ptr var) const
{
    if (!var || !var->address.address)
        return;

    void* objPtr = var->address.ResolveAs<void>();
    if (!objPtr)
        return;

    std::vector<DictEntryRaw> entries(1000);
    size_t                    count = SafeIterateDict(reinterpret_cast<CScriptDictionary*>(objPtr), entries.data(), entries.size());

    for (size_t i = 0; i < count; i++)
    {
        const CString* keyObj = entries[i].keyObj;
        char           keyBuf[257];
        size_t         keyLen    = 0;
        bool           truncated = false;
        std::string    keyName;
        if (keyObj && SafeReadCString(keyObj, keyBuf, sizeof(keyBuf), keyLen, truncated))
        {
            keyName = std::string(keyBuf, keyLen);
        }
        else
        {
            keyName = fmt::format("entry_{}", i);
        }

        void* node = entries[i].node;
        if (node)
        {
            int   valTypeId = *reinterpret_cast<int*>(static_cast<char*>(node) + 0x38);
            void* valAddr   = static_cast<char*>(node) + 0x30;

            if ((valTypeId & asTYPEID_MASK_OBJECT) && !(valTypeId & asTYPEID_OBJHANDLE))
            {
                valAddr = SafeReadPointer(valAddr);
            }

            std::string_view typeName = "";
            if (var->dbg.cache)
                typeName = var->dbg.cache->GetTypeNameFromType({valTypeId, asTM_NONE});

            var->CreateChildVariable(keyName, asIDBVarAddr{valTypeId, false, valAddr}, typeName);
        }
    }
}

const asIDBTypeEvaluator& asIDBCache::GetEvaluator(const asIDBVarAddr& id) const
{
    // the only way the base address is null is if
    // it's uninitialized.
    static constexpr const asIDBUninitTypeEvaluator uninitType;
    static constexpr const asIDBNullTypeEvaluator   nullType;

    if (id.address == nullptr)
        return uninitType;
    else if (id.ResolveAs<void>() == nullptr)
        return nullType;

    // Check registered engine / script types first (including enums, funcdefs, objects, etc.)
    auto type = ctx ? ctx->GetEngine()->GetTypeInfoById(id.typeId) : nullptr;
    if (type)
    {
        if (type->GetFlags() & asOBJ_ENUM)
        {
            static constexpr const asIDBEnumTypeEvaluator enumType;
            return enumType;
        }
        else if (type->GetFlags() & asOBJ_FUNCDEF)
        {
            static constexpr const asIDBFuncDefTypeEvaluator funcdefType;
            return funcdefType;
        }

        const char* typeName = type->GetName();
        if (typeName && strcmp(typeName, "string") == 0)
        {
            static const asIDBStringTypeEvaluator stringType;
            return stringType;
        }
        else if (typeName && (strcmp(typeName, "array") == 0 || (type->GetFlags() & asOBJ_TEMPLATE)))
        {
            static const asIDBArrayTypeEvaluator arrayType;
            return arrayType;
        }
        else if (typeName && strcmp(typeName, "dictionary") == 0)
        {
            static const asIDBDictionaryTypeEvaluator dictType;
            return dictType;
        }

        // finally, just return the base object one.
        static constexpr const asIDBObjectTypeEvaluator objectType;
        return objectType;
    }

    // If no type info exists, check primitive types.
    switch (id.typeId & asTYPEID_MASK_SEQNBR)
    {
        case asTYPEID_BOOL:
        {
            static constexpr const asIDBPrimitiveTypeEvaluator<bool> boolType;
            return boolType;
        }
        case asTYPEID_INT8:
        {
            static constexpr const asIDBPrimitiveTypeEvaluator<int8_t> int8Type;
            return int8Type;
        }
        case asTYPEID_INT16:
        {
            static constexpr const asIDBPrimitiveTypeEvaluator<int16_t> int16Type;
            return int16Type;
        }
        case asTYPEID_INT32:
        {
            static constexpr const asIDBPrimitiveTypeEvaluator<int32_t> int32Type;
            return int32Type;
        }
        case asTYPEID_INT64:
        {
            static constexpr const asIDBPrimitiveTypeEvaluator<int64_t> int64Type;
            return int64Type;
        }
        case asTYPEID_UINT8:
        {
            static constexpr const asIDBPrimitiveTypeEvaluator<uint8_t> uint8Type;
            return uint8Type;
        }
        case asTYPEID_UINT16:
        {
            static constexpr const asIDBPrimitiveTypeEvaluator<uint16_t> uint16Type;
            return uint16Type;
        }
        case asTYPEID_UINT32:
        {
            static constexpr const asIDBPrimitiveTypeEvaluator<uint32_t> uint32Type;
            return uint32Type;
        }
        case asTYPEID_UINT64:
        {
            static constexpr const asIDBPrimitiveTypeEvaluator<uint64_t> uint64Type;
            return uint64Type;
        }
        case asTYPEID_FLOAT:
        {
            static constexpr const asIDBPrimitiveTypeEvaluator<float> floatType;
            return floatType;
        }
        case asTYPEID_DOUBLE:
        {
            static constexpr const asIDBPrimitiveTypeEvaluator<double> doubleType;
            return doubleType;
        }
        case asTYPEID_VOID:
        default:
            return uninitType;
    }
}

#include <filesystem>
#include <fstream>

std::string asIDBFileWorkspace::PathToSection(const std::string_view v) const
{
    return std::filesystem::relative(v, base_path).generic_string();
}

std::string asIDBFileWorkspace::SectionToPath(const std::string_view v) const
{
    return (base_path + '/').append(v);
}

std::string asIDBFileWorkspace::SectionSource(const std::string_view v) const
{
    std::filesystem::path p(SectionToPath(v));
    size_t                size = std::filesystem::file_size(p);
    std::string           data(size, '\0');
    std::ifstream         in(p);
    in.read(data.data(), size);

    // not sure why this is necessary but here we go
    if (auto nul = data.find_first_of('\0'))
        data.erase(nul);

    return data;
}

void asIDBFileWorkspace::CompileScriptSources()
{
    for (auto& engine : engines)
    {
        for (size_t i = 0; i < engine->GetModuleCount(); i++)
        {
            auto module = engine->GetModuleByIndex(i);

            for (size_t f = 0; f < module->GetFunctionCount(); f++)
            {
#if ANGELSCRIPT_VERSION >= 23800
                const char* section;
                module->GetFunctionByIndex(f)->GetDeclaredAt(&section, nullptr, nullptr);
#else
                const char* section = module->GetFunctionByIndex(f)->GetScriptSectionName();
#endif
                if (section) AddSection(section);
            }
        }
    }
}

void asIDBFileWorkspace::CompileBreakpointPositions()
{
    potential_breakpoints.clear();

    auto addFunctionBreakpointLocations = [&](asIScriptFunction* func) {
#if ANGELSCRIPT_VERSION >= 23800
        for (int i = 0; i < func->GetLineEntryCount(); i++)
        {
            const char* section;
            int         line, col;
            func->GetLineEntry(i, &line, &col, &section, nullptr);
            potential_breakpoints[section].insert(asIDBLineCol{line, col});
        }
#else
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
#endif
    };

    for (auto& engine : engines)
    {
        for (size_t i = 0; i < engine->GetModuleCount(); i++)
        {
            asIScriptModule* module = engine->GetModuleByIndex(i);

            for (size_t f = 0; f < module->GetFunctionCount(); f++)
                addFunctionBreakpointLocations(module->GetFunctionByIndex(f));

            for (size_t t = 0; t < module->GetObjectTypeCount(); t++)
            {
                asITypeInfo* type = module->GetObjectTypeByIndex(t);

                for (size_t m = 0; m < type->GetMethodCount(); m++)
                    addFunctionBreakpointLocations(type->GetMethodByIndex(m, false));

                for (size_t m = 0; m < type->GetBehaviourCount(); m++)
                    addFunctionBreakpointLocations(type->GetBehaviourByIndex(m, nullptr));

                for (size_t m = 0; m < type->GetFactoryCount(); m++)
                    addFunctionBreakpointLocations(type->GetFactoryByIndex(m));
            }
        }
    }
}

/*static*/ void asIDBDebugger::LineCallback(asIScriptContext* ctx, asIDBDebugger* debugger)
{
    if (debugger->internal_execution)
        return;

    const char* section = nullptr;
    int         col;
    int         row = ctx->GetLineNumber(0, &col, &section);

    // we might not have an action - functions called from within
    // the debugger will never have this set.
    if (debugger->action != asIDBAction::None)
    {
        // 1. If stepping was initiated from a specific context, do not step into other unrelated contexts
        if (debugger->stepping_context && ctx != debugger->stepping_context)
        {
            // Do not break for foreign context stepping
        }
        // 2. Do not break inside virtual engine sections (like __Global__BaseAnimating) during stepping
        else if (section && debugger->workspace && debugger->workspace->SectionIsVirtual(section))
        {
            // Skip stepping into virtual/native engine glue
        }
        else
        {
            auto        func      = ctx->GetFunction(0);
            const char* funcName  = func ? func->GetName() : nullptr;
            bool        isUnnamed = funcName && std::strcmp(funcName, "_unnamed_function_") == 0;

            // Step Into just breaks on whatever happens to be next (except foreign unnamed functions).
            if (debugger->action == asIDBAction::Pause ||
                debugger->action == asIDBAction::StepInto)
            {
                if (!isUnnamed || debugger->stepping_function_id == (func ? func->GetId() : -1))
                {
                    debugger->DebugBreak(ctx);
                    return;
                }
            }
            // Step Over breaks on the next line that is <= the
            // current stack level.
            else if (debugger->action == asIDBAction::StepOver)
            {
                bool isSameOrCaller = false;
                if (ctx->GetCallstackSize() < debugger->stack_size)
                {
                    isSameOrCaller = true;
                }
                else if (ctx->GetCallstackSize() == debugger->stack_size)
                {
                    if (debugger->stepping_function_id == -1 ||
                        (func && func->GetId() == debugger->stepping_function_id))
                    {
                        isSameOrCaller = true;
                    }
                }

                if (isSameOrCaller)
                {
                    if (!isUnnamed || debugger->stepping_function_id == (func ? func->GetId() : -1))
                    {
                        debugger->DebugBreak(ctx);
                        return;
                    }
                }
            }
            // Step Out breaks on the next line that is < the
            // current stack level.
            else if (debugger->action == asIDBAction::StepOut)
            {
                if (ctx->GetCallstackSize() < debugger->stack_size)
                {
                    if (!isUnnamed || debugger->stepping_function_id == (func ? func->GetId() : -1))
                    {
                        debugger->DebugBreak(ctx);
                        return;
                    }
                }
            }
        }
    }

    // breakpoints are handled here. note that a single
    // breakpoint can be hit by multiple things on the same
    // line.
    bool break_from_bp = false;

    if (section && (!debugger->breakpoints.empty() || !debugger->function_breakpoints.empty()))
    {
        std::scoped_lock lock(debugger->mutex);

        std::string secStr(section);
        // Fast-path: Check negative cache to skip sections without breakpoints
        if (debugger->bp_negative_cache.find(secStr) == debugger->bp_negative_cache.end())
        {
            auto entries = debugger->breakpoints.find(section);
            if (entries == debugger->breakpoints.end() && debugger->workspace)
            {
                std::string resolved = debugger->workspace->PathToSection(section);
                entries              = debugger->breakpoints.find(resolved);
            }
            if (entries == debugger->breakpoints.end())
            {
                for (auto it = debugger->breakpoints.begin(); it != debugger->breakpoints.end(); ++it)
                {
                    if (PathMatchesSection(section, it->first))
                    {
                        entries = it;
                        break;
                    }
                }
            }

            if (entries != debugger->breakpoints.end())
            {
                debugger->bp_section_cache.insert(secStr);
                for (auto& lines : entries->second)
                {
                    if (row == lines.line)
                    {
                        if (!lines.column.has_value() || lines.column.value() <= 0 || lines.column.value() == col)
                        {
                            lines.hitCount++;

                            // Logpoint: send message to DAP console and do not pause game!
                            if (!lines.logMessage.empty())
                            {
                                debugger->SendLogOutput(fmt::format("[Logpoint] {}\n", lines.logMessage));
                                continue;
                            }

                            // Hit count condition: if numeric, break only when hit count >= target
                            if (!lines.hitCondition.empty())
                            {
                                int target = atoi(lines.hitCondition.c_str());
                                if (target > 0 && lines.hitCount < target)
                                    continue;
                            }

                            // Conditional expression: evaluate condition in frame 0
                            if (!lines.condition.empty())
                            {
                                asIDBCache evalCache(*debugger, ctx);
                                auto       exprRes = evalCache.ResolveExpression(lines.condition, 0);
                                if (exprRes.has_value())
                                {
                                    auto var = exprRes.value().lock();
                                    if (var)
                                    {
                                        var->Evaluate();
                                        if (var->value == "false" || var->value == "0" || var->value == "(null)")
                                            continue;
                                    }
                                }
                            }

                            break_from_bp = true;
                            break;
                        }
                    }
                }
            }
            else
            {
                // Remember that this section has no breakpoints so future lines skip all checks
                debugger->bp_negative_cache.insert(secStr);
            }
        }

        if (!debugger->function_breakpoints.empty())
        {
            int  decl_row;
            auto func = ctx->GetFunction(0);

            if (func)
            {
#if ANGELSCRIPT_VERSION >= 23800
                func->GetDeclaredAt(nullptr, &decl_row, nullptr);
#else
                decl_row = func->FindNextLineWithCode(1);
#endif

                if (debugger->function_breakpoints.find(func->GetName()) != debugger->function_breakpoints.end() &&
                    row == func->FindNextLineWithCode(decl_row))
                    break_from_bp = true;
            }
        }
    }

    if (break_from_bp)
        debugger->DebugBreak(ctx);
}

/*static*/ void asIDBDebugger::ExceptionCallback(asIScriptContext* ctx, asIDBDebugger* debugger)
{
    if (debugger->internal_execution)
        return;

    // Only break on exceptions if explicitly enabled by the DAP client (setExceptionBreakpoints)
    if (!debugger->break_on_exceptions)
        return;

    debugger->DebugBreak(ctx);
}

void asIDBDebugger::HookContextOnce(asIScriptContext* ctx)
{
    if (!ctx || ctx->GetState() == asEXECUTION_EXCEPTION)
        return;

    {
        std::scoped_lock lock(mutex);
        if (hooked_contexts.find(ctx) != hooked_contexts.end())
            return;
        hooked_contexts.insert(ctx);
        if (workspace && ctx->GetEngine())
            workspace->engines.insert(ctx->GetEngine());
    }

    ctx->SetLineCallback(asFUNCTION(asIDBDebugger::LineCallback), this, asCALL_CDECL);
    ctx->SetExceptionCallback(asFUNCTION(asIDBDebugger::ExceptionCallback), this, asCALL_CDECL);
}

void asIDBDebugger::ClearHookedContexts()
{
    std::scoped_lock lock(mutex);
    hooked_contexts.clear();
}

void asIDBDebugger::HookContext(asIScriptContext* ctx, bool has_work)
{
    if (ctx->GetState() != asEXECUTION_EXCEPTION)
    {
        if (workspace && ctx->GetEngine())
            workspace->engines.insert(ctx->GetEngine());

        if (has_work)
        {
            ctx->SetLineCallback(asFUNCTION(asIDBDebugger::LineCallback), this, asCALL_CDECL);
        }
        ctx->SetExceptionCallback(asFUNCTION(asIDBDebugger::ExceptionCallback), this, asCALL_CDECL);
    }
}

void asIDBDebugger::DebugBreak(asIScriptContext* ctx)
{
    if (ASDebug_IsVerbose())
    {
        ASDebug_Log("DebugBreak ENTERED! ctx=%p", ctx);
    }
    if (workspace && ctx->GetEngine())
        workspace->engines.insert(ctx->GetEngine());

    {
        std::scoped_lock lock(mutex);
        last_break_action                     = action;
        action                                = asIDBAction::None;
        stepping_context                      = nullptr;
        stepping_function_id                  = -1;
        std::unique_ptr<asIDBCache> new_cache = CreateCache(ctx);

        if (cache)
            new_cache->Restore(*cache);

        std::swap(cache, new_cache);
        UpdateHasWorkAtomic();
    }

    HookContext(ctx, true);
    Suspend();
}

void asIDBDebugger::ClearBreakpointCaches()
{
    bp_section_cache.clear();
    bp_negative_cache.clear();
    for (const auto& [sec, _] : breakpoints)
    {
        bp_section_cache.insert(std::string(sec));
    }
}

void asIDBDebugger::UpdateHasWorkAtomic()
{
    std::scoped_lock lock(mutex);
    ClearBreakpointCaches();
    bool work = (action != asIDBAction::None) || (!breakpoints.empty()) || (!function_breakpoints.empty());
    has_work_atomic.store(work, std::memory_order_release);
}

bool asIDBDebugger::HasWork()
{
    std::scoped_lock lock(mutex);

    bool work = false;
    if (action != asIDBAction::None)
        work = true;
    else if (!breakpoints.empty())
        work = true;
    else if (!function_breakpoints.empty())
        work = true;

    has_work_atomic.store(work, std::memory_order_release);
    return work;
}

// debugger operations; these set the next breakpoint
// and call Resume.
void asIDBDebugger::SetAction(asIDBAction new_action)
{
    // should never happen
    if (new_action == asIDBAction::None)
        return;

    {
        std::scoped_lock lock(mutex);
        if (new_action == asIDBAction::Continue)
        {
            action               = asIDBAction::None;
            stepping_context     = nullptr;
            stepping_function_id = -1;
        }
        else
        {
            action               = new_action;
            stepping_context     = cache ? cache->ctx : nullptr;
            stepping_function_id = (cache && cache->ctx && cache->ctx->GetFunction(0)) ?
                cache->ctx->GetFunction(0)->GetId() :
                -1;

            if (cache)
                stack_size = cache->ctx->GetCallstackSize();
        }
        UpdateHasWorkAtomic();
    }

    Resume();
}

bool asIDBDebugger::ToggleBreakpoint(std::string_view section, int line)
{
    std::scoped_lock lock(mutex);
    auto             it = breakpoints.find(section);

    if (it == breakpoints.end())
        it = breakpoints.emplace(section, asIDBSectionBreakpoints{}).first;

    for (auto lit = it->second.begin(); lit != it->second.end(); lit++)
    {
        if (lit->line == line)
        {
            it->second.erase(lit);

            if (it->second.empty())
                breakpoints.erase(it);

            UpdateHasWorkAtomic();
            return false;
        }
    }

    it->second.push_back({line});
    UpdateHasWorkAtomic();
    return true;
}
