#include "PyrWebSocketPrim.hpp"

extern bool compiledOK;
using namespace wsclang;

/// We redefine headers for these two methods from OSCData.cpp
/// as they are used for reading/writing osc through websocket
pyrobject* ConvertOSCMessage(int sz, char* data);
int makeSynthMsgWithTags(big_scpacket* packet, PyrSlot* slots, int size);

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
wsclang::varread(pyrslot* s, uint16_t index)
{
    return static_cast<T>(slotRawPtr(&slotRawObject(s)->slots[index]));
}

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

template<typename T> inline void
wsclang::varwrite(pyrslot* s, T object, uint16_t index )
{
    SetPtr(slotRawObject(s)->slots+index, object);
}

template<> inline void
wsclang::varwrite(pyrslot* s, int object, uint16_t index)
{
    wsclang::write(slotRawObject(s)->slots+index, object);
}

template<> inline void
wsclang::varwrite(pyrslot* s, std::string object, uint16_t index)
{
    auto str = newPyrString(gMainVMGlobals->gc, object.c_str(), 0, true);
    SetObject(slotRawObject(s)->slots+index, str);
}

template<typename T> void
wsclang::interpret(pyrobject* object, T data, const char* sym)
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

template<typename T> void
wsclang::interpret(pyrobject* object, std::vector<T> data, const char* sym)
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

template<typename T> inline void
wsclang::free(pyrslot* s, T data)
{
    gMainVMGlobals->gc->Free(slotRawObject(s));
    SetNil(s);
    delete data;
}

#ifdef HAVE_AVAHI

void AvahiBrowser::initialize()
{
    int err;
    if ((m_poll = avahi_simple_poll_new()) == nullptr) {
        scpostn_av("error creating simple poll");
        return;
    }
    if ((m_client = avahi_client_new(
                    avahi_simple_poll_get(m_poll),
                    (AvahiClientFlags) 0,
                    client_cb, this, &err)) == nullptr) {
        scpostn_av("error creating new client (%s)",
                   avahi_strerror(err));
        return;
    }
    if ((m_browser = avahi_service_browser_new(
                m_client, AVAHI_IF_UNSPEC, AVAHI_PROTO_INET,
                m_type.c_str(), NULL, (AvahiLookupFlags) 0,
                browser_cb, this)) == nullptr) {
        scpostn_av("error creating service browser (%s)",
                   avahi_strerror(avahi_client_errno(m_client)));
        return;
    }
    start();
}

AvahiBrowser::~AvahiBrowser()
{
    if (m_running)
        stop();
    if (m_browser)
        avahi_service_browser_free(m_browser);
    if (m_client)
        avahi_client_free(m_client);
    if (m_poll)
        avahi_simple_poll_free(m_poll);
}

void
AvahiBrowser::start()
{
    scpostn_av("starting browser thread");
    m_running = true;
    m_thread = std::thread(&AvahiBrowser::poll, this);
}

void
AvahiBrowser::stop()
{
    m_running = false;
    assert(m_thread.joinable());
    m_thread.join();
}

void
AvahiBrowser::poll()
{
    while (m_running)
        avahi_simple_poll_iterate(m_poll, 200);
}

void
AvahiBrowser::add_target(std::string target)
{
    m_targets.push_back(target);
}

void
AvahiBrowser::rem_target(std::string target)
{
    m_targets.erase(std::remove(m_targets.begin(),
                    m_targets.end(), target),
                    m_targets.end());
}

void
AvahiBrowser::resolve_cb(AvahiServiceResolver* r,
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
           AVAHI_GCC_UNUSED void* udt)
{
    switch (event) {
    case AVAHI_RESOLVER_FAILURE: {
        scpostn_av("failed to resolve service %s", name);
        break;
    }
    case AVAHI_RESOLVER_FOUND: {
        char addr[AVAHI_ADDRESS_STR_MAX], pstr[5];
        avahi_address_snprint(addr, AVAHI_ADDRESS_STR_MAX, address);
        sprintf(pstr, "%d", port);
        scpostn_av("service resolved: %s (%s), "
                   "address: %s, port: %s", name, domain, addr, pstr);
        // return data to sclang as an array (for now)
        auto avb = static_cast<AvahiBrowser*>(udt);
        std::vector<std::string> ret = { name, domain, addr, pstr };
        wsclang::interpret<std::string>(avb->object(), ret, "pvOnTargetResolved");
        break;
    }
    }
}

void
AvahiBrowser::browser_cb(avahi_service_browser* browser,
           AvahiIfIndex interface,
           AvahiProtocol protocol,
           AvahiBrowserEvent event,
           const char* name,
           const char* type,
           const char* domain,
           AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
           void* udt)
{
    auto avb = static_cast<AvahiBrowser*>(udt);
    switch (event) {
    case AVAHI_BROWSER_FAILURE: {
        scpostn_av("browser failed");
        avb->m_running = false;
        return;
    }
    case AVAHI_BROWSER_NEW: {
        scpostn_av("service detected: %s (%s)", name, domain);
        for (const auto& target : avb->m_targets) {
            if (name == target) {
                if ((avahi_service_resolver_new(
                         avb->m_client,  interface, protocol,
                         name, type, domain, AVAHI_PROTO_INET,
                         (AvahiLookupFlags) 0, resolve_cb, avb)) == nullptr) {
                    scpostn_av("failed to resolve target %s");
                }
            }
        }
        break;
    }
    case AVAHI_BROWSER_REMOVE: {
        for (const auto& target : avb->m_targets)
             if (name == target)
                 wsclang::interpret(avb->object(), target, "pvOnTargetRemoved");
        break;
    }
    case AVAHI_BROWSER_ALL_FOR_NOW:
    case AVAHI_BROWSER_CACHE_EXHAUSTED:
        break;
    }
}

void
AvahiBrowser::client_cb(avahi_client* client,
          avahi_client_state state,
          void* udt)
{
    auto avb = static_cast<AvahiBrowser*>(udt);
    if (state == AVAHI_CLIENT_FAILURE) {
        scpostn_av("client failure");
        avb->m_running = false;
    }
}

AvahiService::AvahiService(std::string name, std::string type, uint16_t port) :
    m_name(name),
    m_type(type),
    m_port(port)
{
    int err = 0;
    scpostn_av("registering service: %s", m_name.c_str());
    m_poll = avahi_simple_poll_new();
    m_client = avahi_client_new(avahi_simple_poll_get(m_poll),
                 static_cast<AvahiClientFlags>(0),
                 client_cb, this, &err);
    if (err) {
        // memo -26 = daemon not running,
        // with systemd, just do $systemctl enable avahi-daemon.service
        scpostn_av("error creating new client: %d (%s)", err,
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
    // note: no need to free entry_group apparently.
    avahi_client_free(m_client);
    avahi_simple_poll_free(m_poll);
}

void AvahiService::poll()
{
    while (m_running)
        avahi_simple_poll_iterate(m_poll, 200);
}

void AvahiService::group_cb(avahi_entry_group* grp,
                            avahi_entry_group_state state,
                            void* udt)
{
    switch (state) {
    case AVAHI_ENTRY_GROUP_REGISTERING:
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
    case AVAHI_ENTRY_GROUP_UNCOMMITED:
        break;
    case AVAHI_ENTRY_GROUP_COLLISION: {
        scpostn_av("entry group collision");
        break;
    }
    case AVAHI_ENTRY_GROUP_FAILURE: {
        scpostn_av("entry group failure");
        break;
    }
    }
}

void AvahiService::client_cb(avahi_client *client, avahi_client_state state, void *udata)
{
    auto svc = static_cast<AvahiService*>(udata);
    switch(state) {
    case AVAHI_CLIENT_CONNECTING:
    case AVAHI_CLIENT_S_REGISTERING:
        break;
    case AVAHI_CLIENT_S_RUNNING: {
        auto group = svc->m_group;
        if(!group) {
            group  = avahi_entry_group_new(client, group_cb, svc);
            svc->m_group = group;
        }        
        if (avahi_entry_group_is_empty(group)) {
            int err = avahi_entry_group_add_service(group,
                        AVAHI_IF_UNSPEC, AVAHI_PROTO_INET, static_cast<AvahiPublishFlags>(0),
                        svc->m_name.c_str(), svc->m_type.c_str(),
                        nullptr, nullptr, svc->m_port, nullptr);
            if (err) {
                 scpostn_av("failed to add service: %s", avahi_strerror(err));
                 return;
            }
            if ((err = avahi_entry_group_commit(group))) {
                scpostn_av("failed to commit group: %s", avahi_strerror(err));
                return;
            }
        }
        break;
    }
    case AVAHI_CLIENT_FAILURE: {
        scpostn_av("client failure");
        break;
    }
    case AVAHI_CLIENT_S_COLLISION: {
        scpostn_av("client collision");
        break;
    }
    }
}
#endif // HAVE_AVAHI

void Server::initialize()
{
    mg_mgr_init(&m_mginterface, this);
    char s_tcp[5];
    sprintf(s_tcp, "%d", m_port);
    scpostn_mg("binding server socket on port %d", m_port);
    mg_connection* connection = mg_bind(&m_mginterface, s_tcp, ws_event_handler);
    if (connection == nullptr) {
        scpostn_mg("error, could not bind server on port %d", m_port);
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
    m_running = false;
    // don't leave interpreter hanging, just crash the damn thing instead..
    assert(m_mgthread.joinable());
    m_mgthread.join();
    mg_mgr_free(&m_mginterface);
}

static void
parse_websocket_frame(websocket_message* message, pyrobject* dest)
{
    std::string wms(reinterpret_cast<const char*>(message->data), message->size);

    if (message->flags & WEBSOCKET_OP_TEXT) {
        wsclang::interpret(dest, wms, "pvOnTextMessageReceived");
    }
    else if (message->flags & WEBSOCKET_OP_BINARY) {
        // might be OSC
        auto data = reinterpret_cast<char*>(message->data);
        // TODO: we have to check for osc messages and bundles,
        // if not, transmit as raw binary data
        auto array = ConvertOSCMessage(message->size, data);
        wsclang::interpret(dest, array, "pvOnOscMessageReceived");
    } else {
        scpostn_mg("error, unknown websocket message type");
    }
}

void Server::ws_event_handler(mg_connection* mgc, int event, void* data)
{
    auto server = static_cast<Server*>(mgc->mgr->user_data);
    switch(event) {
    case MG_EV_RECV: {
        break;
    }
    case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
        Connection c(mgc);
        server->m_connections.push_back(c);
        // at this point, the pyrobject has not been set
        //it will have to go through the "bind" primitive call first
        wsclang::interpret(server->object(), &server->m_connections.back(),
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
        wsclang::interpret(server->object(), req,
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
                    wsclang::interpret(server->object(), &connection,
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
    m_connection.mgc = mg_connect_ws(&m_ws_mgr, ws_event_handler, ws_addr.c_str(),
                                     nullptr, nullptr);
    assert(m_connection.mgc); //for now
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
    assert(m_thread.joinable());
    m_thread.join();
    mg_mgr_free(&m_ws_mgr);
}

void Client::ws_event_handler(mg_connection* mgc, int event, void* data)
{
    auto client = static_cast<Client*>(mgc->mgr->user_data);
    switch(event) {
    case MG_EV_CONNECT: break;
    case MG_EV_POLL: break;
    case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
         wsclang::interpret(client->object(), &client->m_connection,
                            "pvOnConnected");
        break;
    }   
    case MG_EV_WEBSOCKET_FRAME: {
        auto wm = static_cast<websocket_message*>(data);
        parse_websocket_frame(wm, client->m_connection.object());
        break;
    }
    case MG_EV_HTTP_REPLY: {
        http_message* reply = static_cast<http_message*>(data);        
        mgc->flags |= MG_F_CLOSE_IMMEDIATELY;
        auto req = new HttpRequest(mgc, reply);
        wsclang::interpret(client->object(), req, "pvOnHttpReplyReceived");
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

/// Binds <Connection> sclang object to its mg representation.
/// setting address/port instvars to be accessed from sclang
int
pyr_ws_con_bind(vmglobals* g, int)
{
    auto nc = wsclang::varread<Connection*>(g->sp, 0);
    auto mgc = nc->mgc;
    // write address/port in sc object
    char addr[32], s_port[8];
    mg_sock_addr_to_str(&mgc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP);
    std::string saddr(addr, 32);
    wsclang::varwrite(g->sp, saddr, 1);

    mg_sock_addr_to_str(&mgc->sa, s_port, sizeof(s_port), MG_SOCK_STRINGIFY_PORT);
    std::string strport(s_port, 8);
    int port = std::stoi(strport);
    wsclang::varwrite<int>(g->sp, port, 2);

    nc->set_object(slotRawObject(g->sp));
    return errNone;
}

/// Sends text message to <Connection> through websocket
int
pyr_ws_con_write_text(vmglobals* g, int)
{
    auto nc = wsclang::varread<Connection*>(g->sp-1, 0);
    auto text = wsclang::read<std::string>(g->sp);    
    scpostn_mg("websocket text out: %s", text.data());
    mg_send_websocket_frame(nc->mgc, WEBSOCKET_OP_TEXT,
                            text.c_str(), text.size());
    return errNone;
}

/// Sends OSC message through websocket (binary opcode)
/// using the method from OSCData.cpp
int
pyr_ws_con_write_osc(vmglobals* g, int n)
{
    pyrslot* cslot = g->sp-n+1;
    pyrslot* aslot = cslot+1;
    auto connection = wsclang::varread<Connection*>(cslot, 0);

    big_scpacket packet;
    int err = makeSynthMsgWithTags(&packet, aslot, n-1);
    if (err != errNone)
        return err;
    // still don't know why there's a 4bytes padding before the uri...
    // this is a temporary workaround
    mg_send_websocket_frame(connection->mgc, WEBSOCKET_OP_BINARY,
                            packet.data()+4, packet.size());
    return errNone;
}

/// Sends binary message through websocket.
/// Unimpleted yet.
int
pyr_ws_con_write_binary(vmglobals* g, int)
{
    auto connection = wsclang::varread<Connection*>(g->sp-1, 0);
    return errNone;
}

/// Creates <Client> object, returning it to sclang
/// for further manipulation
int
pyr_ws_client_create(vmglobals* g, int)
{    
    auto client = new Client;
    client->set_object(slotRawObject(g->sp));
    wsclang::varwrite(g->sp, client, 0);
    return errNone;
}

/// Connects <Client> to host<string>:port<int>
/// note: should return error if failed
int
pyr_ws_client_connect(vmglobals* g, int)
{
    auto client = wsclang::varread<Client*>(g->sp-2, 0);
    auto host = wsclang::read<std::string>(g->sp-1);
    auto port = wsclang::read<int>(g->sp);
    client->connect(host, port);
    return errNone;
}

/// Disconnects client from host. Unimplemented
int
pyr_ws_client_disconnect(vmglobals* g, int)
{
    return errNone;
}

/* Frees <Client> object from all heaps */
int
pyr_ws_client_free(vmglobals* g, int)
{
    auto client = wsclang::varread<Client*>(g->sp, 0);
    wsclang::free(g->sp, client);
    return errNone;
}

/// Creates <Server>, running on <port>, returns it to sclang.
int
pyr_ws_server_instantiate_run(vmglobals* g, int)
{
    int port = wsclang::read<int>(g->sp);
    auto server = new Server(port);
    server->set_object(slotRawObject(g->sp-1));
    wsclang::varwrite(g->sp-1, server, 0);
    return errNone;
}

/// Frees <Server> object from all heaps
int
pyr_ws_server_free(vmglobals* g, int)
{
    auto server = wsclang::varread<Server*>(g->sp, 0);
    wsclang::free(g->sp, server);
    return errNone;
}

/// Loads contents of <HttpRequest> in the sclang object's
/// instvariables
int
pyr_http_request_bind(vmglobals* g, int)
{
    auto req = wsclang::varread<HttpRequest*>(g->sp, 0);
    auto hm = req->message;
    std::string method(hm->uri.p, hm->uri.len);
    std::string query(hm->query_string.p, hm->query_string.len);
//    std::string mime; // todo
    std::string contents(hm->body.p, hm->body.len);
    wsclang::varwrite(g->sp, method, 1);
    wsclang::varwrite(g->sp, query, 2);
    wsclang::varwrite(g->sp, contents, 4);
    return errNone;
}

int
pyr_http_send_request(vmglobals* g, int)
{    
    auto client = wsclang::varread<Client*>(g->sp-1, 0);
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

    auto req = wsclang::varread<HttpRequest*>(g->sp-3, 0);    
    req->connection->flags |= MG_F_SEND_AND_CLOSE;
    mg_send_head(req->connection, code, body.length(), mime.data());
    mg_printf(req->connection, "%.*s", (int) body.length(), body.data());    
    return errNone;
}

int
pyr_http_request_free(vmglobals* g, int)
{
    auto rep = wsclang::varread<HttpRequest*>(g->sp, 0);
    wsclang::free(g->sp, rep);
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
    wsclang::varwrite(g->sp-3, serv, 0);
    return errNone;
}

int
pyr_zconf_rem_service(vmglobals* g, int)
{
#ifdef HAVE_AVAHI
    auto serv = wsclang::varread<AvahiService*>(g->sp, 0);
#endif
    wsclang::free(g->sp, serv);
    return errNone;
}

int
pyr_zconf_browser_create(vmglobals* g, int)
{
    auto type = wsclang::read<std::string>(g->sp);
#ifdef HAVE_AVAHI
    auto browser = new AvahiBrowser(type);
#endif
    wsclang::varwrite(g->sp-1, browser, 0);
    browser->set_object(slotRawObject(g->sp-1));
    return errNone;
}

int
pyr_zconf_browser_free(vmglobals* g, int)
{
#ifdef HAVE_AVAHI
    auto browser = wsclang::varread<AvahiBrowser*>(g->sp, 0);
#endif
    wsclang::free(g->sp, browser);
    return errNone;
}

int
pyr_zconf_browser_add_target(vmglobals* g, int)
{
#ifdef HAVE_AVAHI
    auto brw = wsclang::varread<AvahiBrowser*>(g->sp-1, 0);
#endif
    auto target = wsclang::read<std::string>(g->sp);
    brw->add_target(target);
    return errNone;
}

int
pyr_zconf_browser_rem_target(vmglobals* g, int)
{
#ifdef HAVE_AVAHI
    auto brw = wsclang::varread<AvahiBrowser*>(g->sp-1, 0);
#endif
    auto target = wsclang::read<std::string>(g->sp);
    brw->rem_target(target);
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

    WSCLANG_DECLPRIM("_ZeroconfBrowserCreate", pyr_zconf_browser_create, 2, 0);
    WSCLANG_DECLPRIM("_ZeroconfBrowserFree", pyr_zconf_browser_free, 1, 0);
    WSCLANG_DECLPRIM("_ZeroconfBrowserAddTarget", pyr_zconf_browser_add_target, 2, 0);
    WSCLANG_DECLPRIM("_ZeroconfBrowserRemoveTarget", pyr_zconf_browser_rem_target, 2, 0);

    WSCLANG_DECLPRIM("_ZeroconfAddService", pyr_zconf_add_service, 4, 0);
    WSCLANG_DECLPRIM("_ZeroconfRemoveService", pyr_zconf_rem_service, 1, 0);

    WSCLANG_DECLPRIM("_HttpRequestBind", pyr_http_request_bind, 1, 0);
    WSCLANG_DECLPRIM("_HttpReply", pyr_http_reply, 4, 0);
    WSCLANG_DECLPRIM("_HttpRequestFree", pyr_http_request_free, 1, 0);
}
