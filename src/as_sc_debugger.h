#pragma once

#include "as_debugger.h"
#include "as_debugger_dap.h"
#include <atomic>
#include <condition_variable>
#include <mutex>

class asSCDebugger : public asIDBDebugger
{
public:
    asIDBDAPServer* dap_server = nullptr;

    asSCDebugger(asIDBWorkspace* workspace);
    virtual ~asSCDebugger() override;

    virtual std::string FetchSource(const char* section) override;

protected:
    virtual void                        Suspend() override;
    virtual void                        Resume() override;
    virtual std::unique_ptr<asIDBCache> CreateCache(asIScriptContext* ctx) override;

private:
    std::recursive_mutex        m_suspendMutex;
    std::condition_variable_any m_cv;
    std::atomic_bool            m_suspended = false;
};
