#include "PyrWebSocketPrim.hpp"

#define strmaxle 4096

// ------------------------------------------------------------------------------------------------
extern bool compiledOK;
using namespace sclang;

pyrobject*
ConvertOSCMessage(int sz, char* data);

int
makeSynthMsgWithTags(big_scpacket* packet, PyrSlot* slots, int size);

// ------------------------------------------------------------------------------------------------
template<> inline bool
sclang::read(pyrslot* s) { return s->tag == tagTrue; }

template<> inline float
sclang::read(pyrslot* s) { return static_cast<float>(s->u.f); }

template<> inline int
sclang::read(pyrslot* s) { return static_cast<int>(s->u.i); }

template<typename T> inline T
sclang::read(pyrslot* s) { return static_cast<T>(slotRawPtr(s)); }

// ------------------------------------------------------------------------------------------------
template<> inline std::string
sclang::read(pyrslot* s)
// ------------------------------------------------------------------------------------------------
{
    char v[strmaxle];
    slotStrVal(s, v, strmaxle);
    return static_cast<std::string>(v);
}

// ------------------------------------------------------------------------------------------------
template<typename T> inline T
sclang::read(pyrslot* s, uint16_t index)
// ------------------------------------------------------------------------------------------------
{
    return static_cast<T>(slotRawPtr(&slotRawObject(s)->slots[index]));
}

// ------------------------------------------------------------------------------------------------
template<> inline void
sclang::write(pyrslot* s, int v) { SetInt(s, v); }

template<> inline void
sclang::write(pyrslot* s, float v) { SetFloat(s, v); }

template<> inline void
sclang::write(pyrslot* s, double v) { SetFloat(s, v); }

template<> inline void
sclang::write(pyrslot* s, void* v) { SetPtr(s, v); }

template<> inline void
sclang::write(pyrslot* s, bool v) { SetBool(s, v); }

template<> inline void
sclang::write(pyrslot* s, pyrobject* o) { SetObject(s, o); }

template<> inline void
sclang::write(pyrslot* s, char c) { SetChar(s, c); }

template<typename T> inline void
sclang::write(pyrslot* s, T o) { SetPtr(s, o); }

// ------------------------------------------------------------------------------------------------
template<> inline
void sclang::write(pyrslot* s, std::string v)
// ------------------------------------------------------------------------------------------------
{
    auto str = newPyrString(gMainVMGlobals->gc, v.c_str(), 0, true);
    SetObject(s, str);
}

// ------------------------------------------------------------------------------------------------
template<typename T> inline void
sclang::write(pyrslot* s, T object, uint16_t index )
// ------------------------------------------------------------------------------------------------
{
    SetPtr(slotRawObject(s)->slots+index, object);
}

// ------------------------------------------------------------------------------------------------
template<> inline void
sclang::write(pyrslot* s, int object, uint16_t index)
// ------------------------------------------------------------------------------------------------
{
    sclang::write(slotRawObject(s)->slots+index, object);
}

// ------------------------------------------------------------------------------------------------
template<> inline void
sclang::write(pyrslot* s, std::string object, uint16_t index)
// ------------------------------------------------------------------------------------------------
{
    auto str = newPyrString(gMainVMGlobals->gc, object.c_str(), 0, true);
    SetObject(slotRawObject(s)->slots+index, str);
}

// ------------------------------------------------------------------------------------------------
template<typename T> void
sclang::return_data(pyrobject* object, T data, const char* sym)
// ------------------------------------------------------------------------------------------------
{
    gLangMutex.lock();

    if (compiledOK) {
        auto g = gMainVMGlobals;
        g->canCallOS = true;
        ++g->sp; sclang::write<pyrobject*>(g->sp, object);
        ++g->sp; sclang::write<T>(g->sp, data);
        runInterpreter(g, getsym(sym), 2);
        g->canCallOS = false;
    }

    gLangMutex.unlock();
}

// ------------------------------------------------------------------------------------------------
template<typename T> void
sclang::return_data(pyrobject* object, std::vector<T> data, const char* sym)
// ------------------------------------------------------------------------------------------------
{
    gLangMutex.lock();

    if (compiledOK) {
        auto g = gMainVMGlobals;
        g->canCallOS = true;
        ++g->sp;
        sclang::write<pyrobject*>(g->sp, object);
        for (const auto& d : data) {
             ++g->sp;
             sclang::write<T>(g->sp, d);
        }

        runInterpreter(g, getsym(sym), data.size()+1);
        g->canCallOS = false;
    }

    gLangMutex.unlock();
}

// ------------------------------------------------------------------------------------------------
template<typename T> inline void
sclang::free(pyrslot* s, T data)
// ------------------------------------------------------------------------------------------------
{
    gMainVMGlobals->gc->Free(slotRawObject(s));
    SetNil(s);
    delete data;
}

// ------------------------------------------------------------------------------------------------
static void
parse_websocket_frame(websocket_message* message, pyrobject* dest)
// ------------------------------------------------------------------------------------------------
{
    std::string wms(reinterpret_cast<const char*>(message->data), message->size);

    if (message->flags & WEBSOCKET_OP_TEXT) {
        sclang::return_data(dest, wms, "pvOnTextMessageReceived");
    }
    else if (message->flags & WEBSOCKET_OP_BINARY)
    {
        // might be OSC
        auto data = reinterpret_cast<char*>(message->data);

        // binary data starts at byte 4
        // why.. header should've been removed at that point...??
        data += 4;

        // we have to check for osc messages and bundles,
        // if not, transmit as raw binary data
        auto array = ConvertOSCMessage(message->size, data);
        sclang::return_data(dest, array, "pvOnOscMessageReceived");
    }
}

// ------------------------------------------------------------------------------------------------
void
network::Server::ws_event_handler(mg_connection* mgc, int event, void* data)
// ------------------------------------------------------------------------------------------------
{
    auto server = static_cast<Server*>(mgc->mgr->user_data);

    switch(event)
    {
    case MG_EV_RECV:
    {
        break;
    }
    case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
    {
        Connection c(mgc);
        server->m_connections.push_back(c);
        // at this point, the pyrobject has not been set
        //it will have to go through the "bind" primitive call first
        sclang::return_data(server->object, &server->m_connections.back(), "pvOnNewConnection");
        break;
    }
    case MG_EV_WEBSOCKET_FRAME:
    {
        auto wm = static_cast<websocket_message*>(data);

        // lookup connection
        auto connection = std::find(
                    server->m_connections.begin(),
                    server->m_connections.end(), mgc);

        if (connection != server->m_connections.end() && connection->object)
            parse_websocket_frame(wm, connection->object);

        break;
    }
    case MG_EV_HTTP_REQUEST:
    {
        http_message* hm = static_cast<http_message*>(data);
        auto req = new HttpRequest(mgc, hm);
        sclang::return_data(server->object, req, "pvOnHttpRequestReceived");
        break;
    }
    case MG_EV_CLOSE:
    {
        if (mgc->flags & MG_F_IS_WEBSOCKET) {

            if (server->m_running == false)
                // ignore if server is not running
                return;

            network::Connection* to_remove = nullptr;
            for (auto& connection : server->m_connections) {
                if (connection == mgc) {
                    to_remove = &connection;
                    sclang::return_data(server->object, &connection, "pvOnDisconnection");
                }
            }

            if (to_remove)
                server->remove_connection(*to_remove);
        }

        break;
    }
    }
}

// ------------------------------------------------------------------------------------------------
void
network::Client::event_handler(mg_connection* mgc, int event, void* data)
// ------------------------------------------------------------------------------------------------
{
    auto client = static_cast<Client*>(mgc->mgr->user_data);
    switch(event)
    {
    case MG_EV_CONNECT:
    {
        break;
    }
    case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
    {
        sclang::return_data(client->object, &client->m_connection, "pvOnConnected");
        break;
    }
    case MG_EV_POLL:
    {
        break;
    }
    case MG_EV_WEBSOCKET_FRAME:
    {
        auto wm = static_cast<websocket_message*>(data);
        parse_websocket_frame(wm, client->object);
        break;
    }
    case MG_EV_HTTP_REPLY:
    {
        http_message* reply = static_cast<http_message*>(data);        
        mgc->flags != MG_F_CLOSE_IMMEDIATELY;

        auto req = new HttpRequest(mgc, reply);
        sclang::return_data(client->object, req, "pvOnHttpReplyReceived");
        break;
    }
    case MG_EV_CLOSE:
    {

    }
    }
}

// ------------------------------------------------------------------------------------------------
int
pyr_ws_con_bind(VMGlobals* g, int)
// ------------------------------------------------------------------------------------------------
{
    auto nc     = sclang::read<network::Connection*>(g->sp, 0);
    auto mgc    = nc->connection;

    // write address/port in sc object
    char addr[32];
    mg_sock_addr_to_str(&mgc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP);
    std::string saddr(addr, 32);
    sclang::write(g->sp, saddr, 1);

    char s_port[8];
    mg_sock_addr_to_str(&mgc->sa, s_port, sizeof(s_port), MG_SOCK_STRINGIFY_PORT);
    std::string strport(s_port, 8);
    int port = std::stoi(strport);
    sclang::write<int>(g->sp, port, 2);

    nc->object = slotRawObject(g->sp);
    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_ws_con_write_text(VMGlobals* g, int)
// ------------------------------------------------------------------------------------------------
{
    auto nc     = sclang::read<network::Connection*>(g->sp-1, 0);
    auto text   = sclang::read<std::string>(g->sp);

    mg_send_websocket_frame(nc->connection, WEBSOCKET_OP_TEXT, text.c_str(), text.size());
    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_ws_con_write_osc(VMGlobals* g, int n)
// ------------------------------------------------------------------------------------------------
{
    pyrslot* cslot = g->sp-n+1;
    pyrslot* aslot = cslot+1;
    auto connection = sclang::read<network::Connection*>(cslot, 0);

    big_scpacket packet;
    int err = makeSynthMsgWithTags(&packet, aslot, n-1);

    if (err != errNone)
        return err;

    mg_send_websocket_frame(connection->connection, WEBSOCKET_OP_BINARY,
                            packet.data(), packet.size());

    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_ws_con_write_binary(VMGlobals* g, int)
// ------------------------------------------------------------------------------------------------
{
    auto connection = sclang::read<network::Connection*>(g->sp-1, 0);
    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_ws_client_create(VMGlobals* g, int)
// ------------------------------------------------------------------------------------------------
{    
    auto client = new network::Client;
    client->object = slotRawObject(g->sp);

    sclang::write(g->sp, client, 0);
    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_ws_client_connect(VMGlobals* g, int)
// ------------------------------------------------------------------------------------------------
{
    auto client = sclang::read<network::Client*>(g->sp-2, 0);
    auto host = sclang::read<std::string>(g->sp-1);
    auto port = sclang::read<int>(g->sp);

    client->connect(host, port);
    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_ws_client_zconnect(vmglobals* g, int)
// ------------------------------------------------------------------------------------------------
{
    auto client = sclang::read<network::Client*>(g->sp-1, 0);
    auto zchost = sclang::read<std::string>(g->sp);

    // todo
    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_ws_client_disconnect(VMGlobals* g, int)
// ------------------------------------------------------------------------------------------------
{
    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_ws_client_free(VMGlobals* g, int)
// ------------------------------------------------------------------------------------------------
{
    auto client = sclang::read<network::Client*>(g->sp, 0);
    sclang::free(g->sp, client);

    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_ws_server_instantiate_run(VMGlobals* g, int)
// ------------------------------------------------------------------------------------------------
{
    int port = sclang::read<int>(g->sp-2);
    std::string name = sclang::read<std::string>(g->sp-1);
    std::string type = sclang::read<std::string>(g->sp);

    auto server = new network::Server(port, name, type);
    server->object = slotRawObject(g->sp-3);

    sclang::write(g->sp-3, server, 0);
    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_ws_server_free(VMGlobals* g, int)
// ------------------------------------------------------------------------------------------------
{
    auto server = sclang::read<network::Server*>(g->sp, 0);
    sclang::free(g->sp, server);

    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_http_request_bind(vmglobals* g, int)
// loads contents of request in the sc object's instvariables
// ------------------------------------------------------------------------------------------------
{
    auto req = sclang::read<network::HttpRequest*>(g->sp, 0);
    auto hm = req->message;

    std::string method(hm->uri.p, hm->uri.len);
    std::string query(hm->query_string.p, hm->query_string.len);
//    std::string mime; // todo

    std::string contents(hm->body.p, hm->body.len);

    sclang::write(g->sp, method, 1);
    sclang::write(g->sp, query, 2);
    sclang::write(g->sp, contents, 4);

    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_http_send_request(vmglobals* g, int)
// from client
// ------------------------------------------------------------------------------------------------
{    
    auto client = sclang::read<network::Client*>(g->sp-1, 0);
    auto req    = sclang::read<std::string>(g->sp);

    client->request(req);
    return errNone;
}

// ------------------------------------------------------------------------------------------------
int
pyr_http_reply(vmglobals* g, int)
// ------------------------------------------------------------------------------------------------
{
    auto code = sclang::read<int>(g->sp-2);
    auto body = sclang::read<std::string>(g->sp-1);
    auto mime = sclang::read<std::string>(g->sp);

    if (!mime.empty())
        mime.insert(0, "Content-Type: ");

    auto req = sclang::read<network::HttpRequest*>(g->sp-3, 0);
    mg_send_head(req->connection, code, body.length(), mime.data());
    mg_printf(req->connection, "%.*s", (int) body.length(), body.data());

    return errNone;
}

// ------------------------------------------------------------------------------------------------
// PRIMITIVES_INITIALIZATION
//---------------------------
#define WS_DECLPRIM(_s, _f, _n, _v)                     \
definePrimitive( base, index++, _s, _f, _n, _v)

// ------------------------------------------------------------------------------------------------
void
network::initialize()
// ------------------------------------------------------------------------------------------------
{
    int base = nextPrimitiveIndex(), index = 0;

    WS_DECLPRIM  ("_WebSocketConnectionWriteText", pyr_ws_con_write_text, 2, 0);

    WS_DECLPRIM  ("_WebSocketConnectionWriteOsc", pyr_ws_con_write_osc, 1, 1);
    WS_DECLPRIM  ("_WebSocketConnectionWriteBinary", pyr_ws_con_write_binary, 2, 0);
    WS_DECLPRIM  ("_WebSocketConnectionBind", pyr_ws_con_bind, 1, 0);

    WS_DECLPRIM  ("_WebSocketClientCreate", pyr_ws_client_create, 1, 0);
    WS_DECLPRIM  ("_WebSocketClientConnect", pyr_ws_client_connect, 3, 0);
    WS_DECLPRIM  ("_WebSocketClientDisconnect", pyr_ws_client_disconnect, 1, 0);
    WS_DECLPRIM  ("_WebSocketClientZConnect", pyr_ws_client_zconnect, 2, 0);
    WS_DECLPRIM  ("_WebSocketClientRequest", pyr_http_send_request, 2, 0);
    WS_DECLPRIM  ("_WebSocketClientFree", pyr_ws_client_free, 1, 0);

    WS_DECLPRIM  ("_WebSocketServerInstantiateRun", pyr_ws_server_instantiate_run, 4, 0);
    WS_DECLPRIM  ("_WebSocketServerFree", pyr_ws_server_free, 1, 0);

    WS_DECLPRIM  ("_HttpRequestBind", pyr_http_request_bind, 1, 0);
    WS_DECLPRIM  ("_HttpReply", pyr_http_reply, 4, 0);
}
