#pragma once

#include "PyrSymbolTable.h"
#include "PyrSched.h"
#include "PyrPrimitive.h"
#include "PyrKernel.h"
#include "PyrSymbol.h"
#include "PyrInterpreter.h"
#include "GC.h"
#include "SC_LanguageClient.h"
#include "scsynthsend.h"
#include <iostream>
#include <thread>

#include "../dependencies/mongoose/mongoose.h"

using pyrslot   = PyrSlot;
using pyrobject = PyrObject;
using vmglobals = VMGlobals;
using pyrint8array = PyrInt8Array;

#define scpostn_av(_str, ...)                       \
    postfl("[avahi] " _str "\n", ##__VA_ARGS__)

#define scpostn_mg(_str, ...)                       \
    postfl("[mongoose] " _str "\n", ##__VA_ARGS__)

namespace wsclang {

/// Initializes http/websocket primitives
void initialize();

/// Calls <sym> sc-method, passing data as argument
template<typename T> void
interpret(pyrobject* object, T data, const char* sym);

/// Calls <sym> sc-method, passing mutiple data as arguments
template<typename T> void
interpret(pyrobject* object, std::vector<T>, const char* sym);

/// Pushes object <T> to slot <s>
template<typename T> void
write(pyrslot* s, T object);

/// Pushes object <T> to  object's instvar at <index>
template<typename T> void
varwrite(pyrslot* s, T object, uint16_t index);

//// reads object <T> from object's instvar at <index>
template<typename T> T
varread(pyrslot* s, uint16_t index);

/// Reads object <T> from slot <s>
template<typename T> T
read(pyrslot* s);

/// Frees object from slot and heap
template<typename T> void
free(pyrslot* s, T object);

/// Every wsclang class is going to have a pyrobject
/// reference for its sclang representation.
//// This is just for convenience
class Object
{
    pyrobject* m_object = nullptr;

public:
    void set_object(pyrobject* object) {
        m_object = object;
    }
    pyrobject* object() {
        return m_object;
    }
};

/// Associates a sclang Connection pyrobject
/// with a mg_connection. This is primarily
/// used in order to lookup connections from one
/// end or the other.
class Connection : public Object
{
public:
    mg_connection* mgc = nullptr;

    Connection(mg_connection* cn) :
        mgc(cn) {}

    bool operator==(Connection const& rhs) {
        return mgc == rhs.mgc;
    }

    bool operator==(mg_connection* rhs) {
        return mgc == rhs;
    }
};

/// Associates mg http_message with a sclang object
/// and a mg_connection, for replying.
class HttpRequest : public Object
{
public:
    mg_connection* connection = nullptr;
    http_message* message = nullptr;

    HttpRequest(mg_connection* con, http_message* msg) :
        connection(con), message(msg) {}
};

/// A mg websocket server. Storing wsclang Connection objects
/// to be retrieved and manipulated through sclang
class Server : public Object
{
    mg_mgr m_mginterface;
    std::vector<Connection> m_connections;
    std::thread m_mgthread;
    uint16_t m_port = 5678;
    bool m_running = false;

public:
    Server(uint16_t port) : m_port(port) {
        initialize();
    }

    /// Initializes and runs websocket server, binding on <m_port>
    void initialize();

    /// Starts mg thread loop
    void poll();

    /// Mg websocket polling loop
    void mg_poll();

    /// Joins mg/dnssd threads, frees its interfaces
    ~Server();

    /// Websocket event handling for <Server> objects
    static void
    ws_event_handler(mg_connection* mgc, int event, void* data);

    /// Removes connection from storage when disconnected
    void
    remove_connection(Connection const& con) {
        m_connections.erase(std::remove(m_connections.begin(),
                            m_connections.end(), con),
                            m_connections.end());}
};

class Client : public Object
{
    Connection m_connection;
    std::thread m_thread;
    mg_mgr m_ws_mgr;
    std::string m_host;
    uint16_t m_port = 0;
    bool m_running = false;

public:
    Client() : m_connection(nullptr) {
        mg_mgr_init(&m_ws_mgr, this);
    }

    ~Client();

    void connect(std::string host, uint16_t port);
    void request(std::string req);
    void poll();

    static void
    ws_event_handler(mg_connection* mgc, int event, void* data);

};

#if defined(__APPLE__) && !defined(SC_IPHONE)
#include <CoreServices/CoreServices.h>

#elif HAVE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>
#include <avahi-common/timeval.h>
#include <avahi-common/simple-watch.h>

/// this is just some style whim..
using avahi_client              = AvahiClient;
using avahi_simple_poll         = AvahiSimplePoll;
using avahi_entry_group         = AvahiEntryGroup;
using avahi_entry_group_state   = AvahiEntryGroupState;
using avahi_client_state        = AvahiClientState;
using avahi_service_browser     = AvahiServiceBrowser;

class AvahiBrowser : public Object
{
    avahi_client* m_client = nullptr;
    avahi_simple_poll* m_poll = nullptr;
    avahi_service_browser* m_browser = nullptr;
    std::string m_type;
    std::vector<std::string> m_targets;
    std::thread m_thread;
    bool m_running = false;

public:

    AvahiBrowser(std::string type) : m_type(type) {
        initialize();
    }

    AvahiBrowser(std::string type, std::string target) {
        initialize();
    }

    /// Initializes avahi client, poll and browser.
    /// Called from both constructors
    void initialize();

    void add_target(std::string target);
    void rem_target(std::string target);

    void start();
    void poll();
    void stop();

    ~AvahiBrowser();

    // Called whenever a service has been resolved successfully
    /// or timed out.
    static void
    resolve_cb(AvahiServiceResolver* r,
               AVAHI_GCC_UNUSED AvahiIfIndex interface,
               AVAHI_GCC_UNUSED AvahiProtocol protocol,
               AvahiResolverEvent event,
               const char* name,
               const char* type,
               const char* domain,
               const char* host_name,
               const AvahiAddress* address,
               uint16_t port,
               AvahiStringList* txt,
               AvahiLookupResultFlags flags,
               AVAHI_GCC_UNUSED void* udt);

    /// Called whenever a new service becomes available on the LAN
    /// or is removed from the LAN.
    static void
    browser_cb(avahi_service_browser* browser,
               AvahiIfIndex interface,
               AvahiProtocol protocol,
               AvahiBrowserEvent event,
               const char* name,
               const char* type,
               const char* domain,
               AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
               void* udt);

    static void
    client_cb(avahi_client* client,
              avahi_client_state state,
              void* udt);
};

class AvahiService
{
    avahi_simple_poll* m_poll = nullptr;
    avahi_entry_group* m_group = nullptr;
    avahi_client* m_client = nullptr;
    std::thread m_thread;
    std::string m_name;
    std::string m_type;
    uint16_t m_port;
    bool m_running = false;

public:
    AvahiService(std::string name, std::string type, uint16_t port);
    ~AvahiService();

private:
    void poll();

    static void
    group_cb(avahi_entry_group* group, avahi_entry_group_state state, void* udata);

    static void
    client_cb(avahi_client* client, avahi_client_state state, void* udata);
};
#endif
}
