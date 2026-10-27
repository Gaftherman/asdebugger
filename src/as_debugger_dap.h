// MIT Licensed
// see https://github.com/Paril/angelscript-debugger

#pragma once

#include <dap/io.h>
#include <dap/network.h>
#include <dap/protocol.h>

#include <atomic>
#include <memory>
#include <mutex>

class asIDBDebugger;
class asIDBDAPClient;

// implementation of DAP server that connects
// back to an asIDBDebugger.
class asIDBDAPServer
{
public:
    // port to connect to
    int port;
    int actual_port = 0;

    int GetBoundPort() const
    {
        return actual_port > 0 ? actual_port : port;
    }

    // ptr back to debugger
    asIDBDebugger* dbg;

    asIDBDAPServer(int port, asIDBDebugger* debugger);

    virtual ~asIDBDAPServer();

    // queries
    bool ServerActive()
    {
        return server.get() != nullptr;
    }
    bool IsRunning()
    {
        return ServerActive();
    }
    bool ClientConnected()
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        return ServerActive() && client != nullptr;
    }

    // server management
    void StartServer();
    void StopServer();
    void Tick();

    // client management

    template <typename T>
    void SendEventToClient(const T& event)
    {
        const dap::TypeInfo* typeinfo = dap::TypeOf<T>::type();
        SendEventToClient(typeinfo, &event);
    }

protected:
    // server ptr
    std::unique_ptr<dap::net::Server> server;

    // connected client (thread-safe)
    mutable std::mutex              clientMutex;
    std::shared_ptr<asIDBDAPClient> client;

    void ClientConnected(const std::shared_ptr<dap::ReaderWriter>& socket);
    void ClientError(const char* msg);
    void SendEventToClient(const dap::TypeInfo* typeinfo, const void* event);
};