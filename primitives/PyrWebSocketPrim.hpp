#pragma once

#if defined(__APPLE__) && !defined(SC_IPHONE)
#include <CoreServices/CoreServices.h>

#elif HAVE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>
#include <avahi-common/timeval.h>
#include <avahi-common/simple-watch.h>

using avahi_client = AvahiClient;
using avahi_simple_poll = AvahiSimplePoll;
using avahi_entry_group = AvahiEntryGroup;
using avahi_entry_group_state = AvahiEntryGroupState;
using avahi_client_state = AvahiClientState;
#endif

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

namespace wsclang {

/* Initializes http/websocket primitives */
void initialize();

/* Calls <sym> sc-method, passing data as argument */
template<typename T> void
return_data(pyrobject* object, T data, const char* sym);

/* Calls <sym> sc-method, passing mutiple data as arguments */
template<typename T> void
return_data(pyrobject* object, std::vector<T>, const char* sym);

/* Pushes object <T> to slot <s> */
template<typename T> void
write(pyrslot* s, T object);

/* Pushes object <T> to  object's instvar at <index> */
template<typename T> void
write(pyrslot* s, T object, uint16_t index);

/* reads object <T> from object's instvar at <index> */
template<typename T> T
read(pyrslot* s, uint16_t index);

/* Reads object <T> from slot <s> */
template<typename T> T
read(pyrslot* s);

/* Frees object from slot and heap */
template<typename T> void
free(pyrslot* s, T object);

/* Every wsclang class is going to have a pyrobject
 * reference for its sclang representation.
 * This is just for convenience */
class Object
{
    pyrobject* m_object = nullptr;

public:
    void set_object(pyrobject* object)
    {
        m_object = object;
    }
    pyrobject* object()
    {
        return m_object;
    }
};

/* Associates a sclang Connection pyrobject
 * with a mg_connection. This is primarily
 * used in order to lookup connections from one
 * end or the other */
class Connection : public Object
{
public:
    mg_connection* connection = nullptr;

    Connection(mg_connection* mgc) :
        connection(mgc) {}

    bool operator==(Connection const& rhs) {
        return connection == rhs.connection;
    }

    bool operator==(mg_connection* rhs) {
        return connection == rhs;
    }
};

/* Associates mg http_message with a sclang object
 * and a mg_connection, for replying. */
class HttpRequest : public Object
{
public:
    mg_connection* connection = nullptr;
    http_message* message = nullptr;

    HttpRequest(mg_connection* con, http_message* msg) :
        connection(con), message(msg) {}
};

#ifdef HAVE_AVAHI
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
    AvahiService(std::string name,
                 std::string type,
                 uint16_t port);

    ~AvahiService();

private:
    void poll();

    static void
    group_callback(avahi_entry_group* group,
                   avahi_entry_group_state state,
                   void* udata);

    static void
    client_callback(avahi_client* client,
                    avahi_client_state state,
                    void* udata);
};
#endif

/* A mg websocket server, embedding dnssd capabilities
 * (the two might be separated in the future). Storing
 * wsclang Connection objects to be retrieved and manipulated
 * through sclang */
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

    /* Initializes and runs websocket server, binding on <m_port>
     * starting dnssd as well (for now) */
    void initialize();

    /* Starts mg/dnssd thread loops */
    void poll();

    /* Mg websocket polling loop */
    void mg_poll();

    /* Joins mg/dnssd threads, frees its interfaces */
    ~Server();

    /* Websocket event handling for <Server> objects */
    static void
    ws_event_handler(mg_connection* mgc, int event, void* data);

    /* Removes connection from storage when disconnected */
    void remove_connection(Connection const& con)
    {
        m_connections.erase(std::remove(m_connections.begin(),
                            m_connections.end(), con),
                            m_connections.end());
    }
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

}
