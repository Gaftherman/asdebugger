#include "as_sc_debugger.h"
#include <chrono>
#include <thread>

asSCDebugger::asSCDebugger(asIDBWorkspace* workspace) :
    asIDBDebugger(workspace)
{
}

asSCDebugger::~asSCDebugger()
{
    Resume();
}

std::string asSCDebugger::FetchSource(const char* section)
{
    if (workspace && section)
        return workspace->SectionSource(section);
    return {};
}

void asSCDebugger::Suspend()
{
    if (dap_server)
    {
        dap::StoppedEvent event;
        switch (action)
        {
            case asIDBAction::Pause:
                event.reason = "pause";
                break;
            case asIDBAction::StepInto:
            case asIDBAction::StepOver:
            case asIDBAction::StepOut:
                event.reason = "step";
                break;
            default:
                event.reason = "breakpoint";
                break;
        }

        event.threadId          = 1;
        event.allThreadsStopped = true;
        dap_server->SendEventToClient(event);
    }

    std::unique_lock<std::mutex> lock(m_suspendMutex);
    m_suspended = true;

    while (m_suspended.load())
    {
        m_cv.wait_for(lock, std::chrono::milliseconds(15), [this] {
            return !m_suspended.load();
        });

        if (dap_server)
            dap_server->Tick();
    }
}

void asSCDebugger::Resume()
{
    {
        std::scoped_lock lock(m_suspendMutex);
        m_suspended = false;
    }
    m_cv.notify_all();
}

std::unique_ptr<asIDBCache> asSCDebugger::CreateCache(asIScriptContext* ctx)
{
    return std::make_unique<asIDBCache>(*this, ctx);
}
