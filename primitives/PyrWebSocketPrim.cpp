#include "PyrWebSocketPrim.hpp"

extern bool compiledOK;
using namespace wsclang;

/* We redefine headers for these two methods from OSCData.cpp
 * as they are used for reading/writing osc through websocket */
pyrobject* ConvertOSCMessage(int sz, char* data);
int makeSynthMsgWithTags(big_scpacket* packet, PyrSlot* slots, int size);

/* Reads object 'T' from slot 's' */
template<> inline bool
wsclang::read(pyrslot* s) { return s->tag == tagTrue; }

template<> inline float
wsclang::read(pyrslot* s) { return static_cast<float>(s->u.f); }

template<> inline int
wsclang::read(pyrslot* s) { return static_cast<int>(s->u.i); }

template<typename T> inline T
wsclang::read(pyrslot* s) { return static_cast<T>(slotRawPtr(s)); }

template<> inline std::string
wsclang::read(pyrslot* s)
{       
    return std::string(slotRawString(s)->s, slotStrLen(s));
}

template<typename T> inline T
wsclang::read(pyrslot* s, uint16_t index)
{
    return static_cast<T>(slotRawPtr(&slotRawObject(s)->slots[index]));
}

/* Pushes object 'T' to slot 's', valid for
 * - int
 * - float/double
 * - boolean
 * - pyrobject
 * - char
 * - std::string
 * - void (raw) ptr
 */
template<> inline void
wsclang::write(pyrslot* s, int v) { SetInt(s, v); }

template<> inline void
wsclang::write(pyrslot* s, float v) { SetFloat(s, v); }

template<> inline void
wsclang::write(pyrslot* s, double v) { SetFloat(s, v); }

template<> inline void
wsclang::write(pyrslot* s, void* v) { SetPtr(s, v); }

template<> inline void
wsclang::write(pyrslot* s, bool v) { SetBool(s, v); }

template<> inline void
wsclang::write(pyrslot* s, pyrobject* o) { SetObject(s, o); }

template<> inline void
wsclang::write(pyrslot* s, char c) { SetChar(s, c); }

template<typename T> inline void
wsclang::write(pyrslot* s, T o) { SetPtr(s, o); }

template<> inline void
wsclang::write(pyrslot* s, std::string v)
{
    auto str = newPyrString(gMainVMGlobals->gc, v.c_str(), 0, true);
    SetObject(s, str);
}

/* Pushes object 'T' to  object's instvar 'index' */
template<typename T> inline void
wsclang::write(pyrslot* s, T object, uint16_t index )
{
    SetPtr(slotRawObject(s)->slots+index, object);
}

template<> inline void
wsclang::write(pyrslot* s, int object, uint16_t index)
{
    wsclang::write(slotRawObject(s)->slots+index, object);
}

template<> inline void
wsclang::write(pyrslot* s, std::string object, uint16_t index)
{
    auto str = newPyrString(gMainVMGlobals->gc, object.c_str(), 0, true);
    SetObject(slotRawObject(s)->slots+index, str);
}

/* Calls 'sym' sc-method, passing data as argument */
template<typename T> void
wsclang::return_data(pyrobject* object, T data, const char* sym)
{
    gLangMutex.lock();
    if (compiledOK) {
        auto g = gMainVMGlobals;
        g->canCallOS = true;
        ++g->sp; wsclang::write<pyrobject*>(g->sp, object);
        ++g->sp; wsclang::write<T>(g->sp, data);
        runInterpreter(g, getsym(sym), 2);
        g->canCallOS = false;
    }
    gLangMutex.unlock();
}

/* Calls 'sym' sc-method, passing mutiple data as arguments */
template<typename T> void
wsclang::return_data(pyrobject* object, std::vector<T> data, const char* sym)
{
    gLangMutex.lock();
    if (compiledOK) {
        auto g = gMainVMGlobals;
        g->canCallOS = true;
        ++g->sp;
        wsclang::write<pyrobject*>(g->sp, object);
        for (const auto& d : data) {
             ++g->sp;
             wsclang::write<T>(g->sp, d);
        }
        runInterpreter(g, getsym(sym), data.size()+1);
        g->canCallOS = false;
    }
    gLangMutex.unlock();
}

/* Frees object from standard heap and sclang's heap
 * sets objects to nil */
template<typename T> inline void
wsclang::free(pyrslot* s, T data)
{
    gMainVMGlobals->gc->Free(slotRawObject(s));
    SetNil(s);
    delete data;
}

#ifdef HAVE_AVAHI
AvahiService::AvahiService(std::string name,
                           std::string type,
                           uint16_t port) :
    m_name(name),
    m_type(type),
    m_port(port)
{
    int err = 0;
    postfl("[avahi] registering service: %s\n", m_name.c_str());
    m_poll = avahi_simple_poll_new();
    m_client = avahi_client_new(avahi_simple_poll_get(m_poll),
                 static_cast<AvahiClientFlags>(0),
                 client_callback, this, &err);
    if (err) {
        // memo -26 = daemon not running,
        // with systemd, just do $systemctl enable avahi-daemon.service
        postfl("[avahi] error creating new client: %d (%s)\n", err,
               avahi_strerror(err));
    } else {
        m_running = true;
        m_thread = std::thread(&AvahiService::poll, this);
    }
}

AvahiService::~AvahiService()
{
    m_running = false;
    assert(m_thread.joinable());
    m_thread.join();

    avahi_client_free(m_client);
    avahi_simple_poll_free(m_poll);
    avahi_entry_group_free(m_group);
}

void AvahiService::poll()
{
    while (m_running)
        avahi_simple_poll_iterate(m_poll, 200);
}

void AvahiService::group_callback(avahi_entry_group *group,
                                  avahi_entry_group_state state,
                                  void *udata)
{
    switch(state) {
    case AVAHI_ENTRY_GROUP_REGISTERING:
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
    case AVAHI_ENTRY_GROUP_UNCOMMITED:
        break;
    case AVAHI_ENTRY_GROUP_COLLISION: {
        postfl("[avahi] entry group collision\n");
        break;
    }
    case AVAHI_ENTRY_GROUP_FAILURE: {
        postfl("[avahi] entry group failure\n");
        break;
    }
    }
}

void AvahiService::client_callback(avahi_client *client,
                                   avahi_client_state state,
                                   void *udata)
{
    auto svc = static_cast<AvahiService*>(udata);
    switch(state) {
    case AVAHI_CLIENT_CONNECTING:
    case AVAHI_CLIENT_S_REGISTERING:
        break;
    case AVAHI_CLIENT_S_RUNNING: {
        postfl("[avahi] client running\n");
        auto group = svc->m_group;
        if(!group) {
            postfl("[avahi] creating entry group\n");
            group  = avahi_entry_group_new(client, group_callback, svc);
            svc->m_group = group;
        }
        if (avahi_entry_group_is_empty(group)) {
            postfl("[avahi] adding service\n");
            int err = avahi_entry_group_add_service(group,
                        AVAHI_IF_UNSPEC, AVAHI_PROTO_INET, static_cast<AvahiPublishFlags>(0),
                        svc->m_name.c_str(), svc->m_type.c_str(),
                        nullptr, nullptr, svc->m_port, nullptr);
            if (err) {
                 postfl("[avahi] Failed to add service: %s\n", avahi_strerror(err));
                 return;
            }
            postfl("[avahi] commiting service\n");
            if ((err = avahi_entry_group_commit(group))) {
                postfl("[avahi] Failed to commit group: %s\n", avahi_strerror(err));
                return;
            }
        }
        break;
    }
    case AVAHI_CLIENT_FAILURE: {
        postfl("[avahi] client failure\n");
        break;
    }
    case AVAHI_CLIENT_S_COLLISION: {
        postfl("[avahi] client collision\n");
        break;
    }
    }
}
#endif // HAVE_AVAHI

/* Initializes and runs websocket server, binding on <m_port>
 * starting dnssd as well (for now) */
void Server::initialize()
{
    mg_mgr_init(&m_mginterface, this);
    char s_tcp[5];
    sprintf(s_tcp, "%d", m_port);
    postfl("[websocket] binding server socket on port %d\n", m_port);
    mg_connection* connection = mg_bind(&m_mginterface, s_tcp, ws_event_handler);
    if (connection == nullptr) {
        postfl("[websocket] error, could not bind server on port %d\n", m_port);
        return;
    }
    mg_set_protocol_http_websocket(connection);
    m_running = true;
    m_mgthread = std::thread(&Server::mg_poll, this);
}


void Server::mg_poll()
{
    while (m_running)
        mg_mgr_poll(&m_mginterface, 200);
}

Server::~Server()
{
    postfl("[websocket] destroying server\n");
    m_running = false;
    // don't leave interpreter hanging, just crash the damn thing instead..
    assert(m_mgthread.joinable());
    m_mgthread.join();
    mg_mgr_free(&m_mginterface);
}

/* Redirects websocket frame to its proper sclang method (text/osc) */
static void
parse_websocket_frame(websocket_message* message, pyrobject* dest)
{
    std::string wms(reinterpret_cast<const char*>(message->data), message->size);

    if (message->flags & WEBSOCKET_OP_TEXT) {
        wsclang::return_data(dest, wms, "pvOnTextMessageReceived");
    }
    else if (message->flags & WEBSOCKET_OP_BINARY) {
        // might be OSC
        auto data = reinterpret_cast<char*>(message->data);
        // binary data starts at byte 4
        // why.. header should've been removed at that point...??
        // data += 4;
        // we have to check for osc messages and bundles,
        // if not, transmit as raw binary data
        auto array = ConvertOSCMessage(message->size, data);
        wsclang::return_data(dest, array, "pvOnOscMessageReceived");
    }
}

/* Websocket event handling for <Server> objects */
void Server::ws_event_handler(mg_connection* mgc, int event, void* data)
{
    auto server = static_cast<Server*>(mgc->mgr->user_data);
    switch(event) {
    case MG_EV_RECV: break;
    case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
        Connection c(mgc);
        server->m_connections.push_back(c);
        // at this point, the pyrobject has not been set
        //it will have to go through the "bind" primitive call first
        wsclang::return_data(server->object(), &server->m_connections.back(),
                            "pvOnNewConnection");
        break;
    }
    case MG_EV_WEBSOCKET_FRAME: {
        auto wm = static_cast<websocket_message*>(data);
        // lookup <Connection> object stored in <Server>
        auto connection = std::find(
                    server->m_connections.begin(),
                    server->m_connections.end(), mgc);
        if (connection != server->m_connections.end() && connection->object())
            parse_websocket_frame(wm, connection->object());
        break;
    }
    case MG_EV_HTTP_REQUEST: {
        http_message* hm = static_cast<http_message*>(data);
        auto req = new HttpRequest(mgc, hm);
        wsclang::return_data(server->object(), req,
                            "pvOnHttpRequestReceived");
        break;
    }
    case MG_EV_CLOSE: {
        if (mgc->flags & MG_F_IS_WEBSOCKET) {
            if (server->m_running == false)
                // ignore if server is not running
                // or else it causes bugs/loops
                return;
            Connection* to_remove = nullptr;
            for (auto& connection : server->m_connections) {
                if (connection == mgc) {
                    to_remove = &connection;
                    wsclang::return_data(server->object(), &connection,
                                        "pvOnDisconnection");
                }
            }
            if (to_remove)
                server->remove_connection(*to_remove);
        }
        break;
    }
    }
}

void Client::connect(std::string host, uint16_t port)
{
    m_host = host;
    m_port = port;
    std::string ws_addr("ws://");
    ws_addr.append(host);
    ws_addr.append(":");
    ws_addr.append(std::to_string(port));
    m_connection.connection = mg_connect_ws(&m_ws_mgr, ws_event_handler, ws_addr.c_str(),
                                            nullptr, nullptr);
    assert(m_connection.connection); //for now
    m_running = true;
    m_thread = std::thread(&Client::poll, this);
}

void Client::request(std::string req)
{
    std::string addr(m_host);
    addr.append(":");
    addr.append(std::to_string(m_port));
    addr.append(req);
    auto mgc = mg_connect_http(&m_ws_mgr, ws_event_handler, addr.data(),
                               nullptr, nullptr);
}

Client::~Client()
{
    m_running = false;
    postfl("[websocket] destroying client\n");
    assert(m_thread.joinable());
    m_thread.join();
    mg_mgr_free(&m_ws_mgr);
}

/* Websocket event handling for <Client> objects */
void Client::ws_event_handler(mg_connection* mgc, int event, void* data)
{
    auto client = static_cast<Client*>(mgc->mgr->user_data);
    switch(event) {
    case MG_EV_CONNECT: break;
    case MG_EV_POLL: break;
    case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
         wsclang::return_data(client->object(), &client->m_connection,
                            "pvOnConnected");
        break;
    }   
    case MG_EV_WEBSOCKET_FRAME: {
        auto wm = static_cast<websocket_message*>(data);
        parse_websocket_frame(wm, client->object());
        break;
    }
    case MG_EV_HTTP_REPLY: {
        http_message* reply = static_cast<http_message*>(data);        
        mgc->flags |= MG_F_CLOSE_IMMEDIATELY;
        auto req = new HttpRequest(mgc, reply);
        wsclang::return_data(client->object(), req, "pvOnHttpReplyReceived");
        break;
    }
    case MG_EV_CLOSE: break;
    }
}

void Client::poll()
{
    while (m_running) {
          mg_mgr_poll(&m_ws_mgr, 200);
    }
}

/* Binds <Connection> sclang object to its mg representation.
 * setting address/port instvars to be accessed from sclang */
int
pyr_ws_con_bind(vmglobals* g, int)
{
    auto nc = wsclang::read<Connection*>(g->sp, 0);
    auto mgc = nc->connection;
    // write address/port in sc object
    char addr[32], s_port[8];
    mg_sock_addr_to_str(&mgc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP);
    std::string saddr(addr, 32);
    wsclang::write(g->sp, saddr, 1);

    mg_sock_addr_to_str(&mgc->sa, s_port, sizeof(s_port), MG_SOCK_STRINGIFY_PORT);
    std::string strport(s_port, 8);
    int port = std::stoi(strport);
    wsclang::write<int>(g->sp, port, 2);

    nc->set_object(slotRawObject(g->sp));
    return errNone;
}

/* Sends text message to <Connection> through websocket */
int
pyr_ws_con_write_text(vmglobals* g, int)
{
    auto nc = wsclang::read<Connection*>(g->sp-1, 0);
    auto text = wsclang::read<std::string>(g->sp);    
    std::cout << "[websocket-text-out]" << text << std::endl;
    mg_send_websocket_frame(nc->connection, WEBSOCKET_OP_TEXT,
                            text.c_str(), text.size());
    return errNone;
}

/* Sends OSC message through websocket (binary opcode)
 * using the method from OSCData.cpp */
int
pyr_ws_con_write_osc(vmglobals* g, int n)
{
    pyrslot* cslot = g->sp-n+1;
    pyrslot* aslot = cslot+1;
    auto connection = wsclang::read<Connection*>(cslot, 0);

    big_scpacket packet;
    int err = makeSynthMsgWithTags(&packet, aslot, n-1);
    if (err != errNone)
        return err;
    mg_send_websocket_frame(connection->connection, WEBSOCKET_OP_BINARY,
                            packet.data(), packet.size());
    return errNone;
}

/* Sends binary message through websocket
 * unimpleted yet. */
int
pyr_ws_con_write_binary(vmglobals* g, int)
{
    auto connection = wsclang::read<Connection*>(g->sp-1, 0);
    return errNone;
}

/* Creates <Client> object, returning it to sclang
 * for further manipulation */
int
pyr_ws_client_create(vmglobals* g, int)
{    
    auto client = new Client;
    client->set_object(slotRawObject(g->sp));
    wsclang::write(g->sp, client, 0);
    return errNone;
}

/* Connects <Client> to host<string>:port<int>
 * note: should return error if failed */
int
pyr_ws_client_connect(vmglobals* g, int)
{
    auto client = wsclang::read<Client*>(g->sp-2, 0);
    auto host = wsclang::read<std::string>(g->sp-1);
    auto port = wsclang::read<int>(g->sp);
    client->connect(host, port);
    return errNone;
}

/* Connects client with zeroconf host, unimplemented */
int
pyr_ws_client_zconnect(vmglobals* g, int)
{
    auto client = wsclang::read<Client*>(g->sp-1, 0);
    auto zchost = wsclang::read<std::string>(g->sp);

    // todo
    return errNone;
}

/* Disconnects client from host. Unimplemented */
int
pyr_ws_client_disconnect(vmglobals* g, int)
{
    return errNone;
}

/* Frees <Client> object from all heaps */
int
pyr_ws_client_free(vmglobals* g, int)
{
    auto client = wsclang::read<Client*>(g->sp, 0);
    wsclang::free(g->sp, client);
    return errNone;
}

/* Creates <Server>, running with dnssd <name>,
 * running on <port>, returns it to sclang */
int
pyr_ws_server_instantiate_run(vmglobals* g, int)
{
    int port = wsclang::read<int>(g->sp);
    auto server = new Server(port);
    server->set_object(slotRawObject(g->sp-1));
    wsclang::write(g->sp-1, server, 0);
    return errNone;
}

/* Frees <Server> object from all heaps */
int
pyr_ws_server_free(vmglobals* g, int)
{
    auto server = wsclang::read<Server*>(g->sp, 0);
    wsclang::free(g->sp, server);
    return errNone;
}

/* Loads contents of <HttpRequest> in the sclang object's
 *  instvariables */
int
pyr_http_request_bind(vmglobals* g, int)
{
    auto req = wsclang::read<HttpRequest*>(g->sp, 0);
    auto hm = req->message;
    std::string method(hm->uri.p, hm->uri.len);
    std::string query(hm->query_string.p, hm->query_string.len);
//    std::string mime; // todo
    std::string contents(hm->body.p, hm->body.len);
    wsclang::write(g->sp, method, 1);
    wsclang::write(g->sp, query, 2);
    wsclang::write(g->sp, contents, 4);
    return errNone;
}

// from client
int
pyr_http_send_request(vmglobals* g, int)
{    
    auto client = wsclang::read<Client*>(g->sp-1, 0);
    auto req = wsclang::read<std::string>(g->sp);
    client->request(req);
    return errNone;
}

int
pyr_http_reply(vmglobals* g, int)
{
    auto code = wsclang::read<int>(g->sp-2);
    auto body = wsclang::read<std::string>(g->sp-1);
    auto mime = wsclang::read<std::string>(g->sp);
    if (!mime.empty())
        mime.insert(0, "Content-Type: ");

    auto req = wsclang::read<HttpRequest*>(g->sp-3, 0);
    mg_send_head(req->connection, code, body.length(), mime.data());
    mg_printf(req->connection, "%.*s", (int) body.length(), body.data());
    return errNone;
}

int
pyr_zconf_add_service(vmglobals* g, int)
{
    auto name = wsclang::read<std::string>(g->sp-2);
    auto type = wsclang::read<std::string>(g->sp-1);
    auto port = wsclang::read<int>(g->sp);
#ifdef HAVE_AVAHI
    auto serv = new AvahiService(name, type, port);
#endif
    wsclang::write(g->sp-3, serv, 0);
    return errNone;
}

int
pyr_zconf_rem_service(vmglobals* g, int)
{
#ifdef HAVE_AVAHI
    auto serv = wsclang::read<AvahiService*>(g->sp, 0);
#endif
    wsclang::free(g->sp, serv);
    return errNone;
}

// -----------------------------------------------------------
// PRIMITIVES INIT
//------------------------------------------------------------
#define WSCLANG_DECLPRIM(_s, _f, _n, _v)                     \
    definePrimitive( base, index++, _s, _f, _n, _v)

void
wsclang::initialize()
{
    int base = nextPrimitiveIndex(), index = 0;

    WSCLANG_DECLPRIM("_WebSocketConnectionWriteText", pyr_ws_con_write_text, 2, 0);
    WSCLANG_DECLPRIM("_WebSocketConnectionWriteOsc", pyr_ws_con_write_osc, 1, 1);
    WSCLANG_DECLPRIM("_WebSocketConnectionWriteBinary", pyr_ws_con_write_binary, 2, 0);
    WSCLANG_DECLPRIM("_WebSocketConnectionBind", pyr_ws_con_bind, 1, 0);

    WSCLANG_DECLPRIM("_WebSocketClientCreate", pyr_ws_client_create, 1, 0);
    WSCLANG_DECLPRIM("_WebSocketClientConnect", pyr_ws_client_connect, 3, 0);
    WSCLANG_DECLPRIM("_WebSocketClientDisconnect", pyr_ws_client_disconnect, 1, 0);
    WSCLANG_DECLPRIM("_WebSocketClientRequest", pyr_http_send_request, 2, 0);
    WSCLANG_DECLPRIM("_WebSocketClientFree", pyr_ws_client_free, 1, 0);

    WSCLANG_DECLPRIM("_WebSocketServerInstantiateRun", pyr_ws_server_instantiate_run, 2, 0);
    WSCLANG_DECLPRIM("_WebSocketServerFree", pyr_ws_server_free, 1, 0);

    WSCLANG_DECLPRIM("_ZeroconfAddService", pyr_zconf_add_service, 4, 0);
    WSCLANG_DECLPRIM("_ZeroconfRemoveService", pyr_zconf_rem_service, 1, 0);

    WSCLANG_DECLPRIM("_HttpRequestBind", pyr_http_request_bind, 1, 0);
    WSCLANG_DECLPRIM("_HttpReply", pyr_http_reply, 4, 0);
}
