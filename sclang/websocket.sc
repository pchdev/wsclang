WebSocketConnection
{
	var m_ptr;
	var <address;
	var <port;
	var textMessageCallback;
	var binaryMessageCallback;
	var oscMessageCallback;

	*new {
		^super.new;
	}

	*newInit { |ptr|
		^this.newCopyArgs(ptr).prmBind();
	}

	initialize { |ptr|
		m_ptr = ptr;
		this.prmBind();
	}

	prmBind {
		_WebSocketConnectionBind
		^this.primitiveFailed
	}

	onTextMessageReceived_ { |callback|
		textMessageCallback = callback;
	}

	onBinaryMessageReceived_ { |callback|
		binaryMessageCallback = callback;
	}

	onOscMessageReceived_ { |callback|
		oscMessageCallback = callback;
	}

	// prim-callbacks ---------------------------

	pvOnTextMessageReceived { |message|
		textMessageCallback.value(message);
	}

	pvOnBinaryMessageReceived { |message|
		binaryMessageCallback.value(message);
	}

	pvOnOscMessageReceived { |address, arguments|
		oscMessageCallback.value(address, arguments);
	}

	writeText { |msg|
		_WebSocketConnectionWriteText
		^this.primitiveFailed
	}

	writeOsc { |...array|
		_WebSocketConnectionWriteOsc
		^this.primitiveFailed
	}

	writeOscClient { |array|
		_WebSocketConnectionWriteOsc
		^this.primitiveFailed
	}

	writeBinary { |data|
		_WebSocketConnectionWriteBinary
		^this.primitiveFailed
	}
}

WebSocketClient
{
	var m_ptr;
	var m_connection;
	var <connected;
	var m_ccb; // connected
	var m_dcb; // disconnected
	var m_http_cb;

	classvar g_instances;

	*initClass {
		g_instances = [];
		ShutDown.add({
			g_instances.do(_.free());
		})
	}

	// CREATE -------------------------------

	*new {
		^super.new.wsClientCtor().primCreate();
	}

	wsClientCtor {
		connected = false;
		g_instances = g_instances.add(this);
		m_connection = WebSocketConnection();
	}

	primCreate {
		_WebSocketClientCreate
		^this.primitiveFailed
	}

	// CONNECTION/DISCONNECTION ------------------------------

	connect { |ip, port|
		_WebSocketClientConnect
		^this.primitiveFailed
	}

	zconnect { |zc_name| // zeroconf connection
		_WebSocketClientZConnect
		^this.primitiveFailed
	}

	disconnect {
		_WebSocketClientDisconnect
		^this.primitiveFailed
	}

	onConnected_ { |callback|
		m_ccb = callback;
	}

	onDisconnected_ { |callback|
		m_dcb = callback;
	}

	pvOnConnected { |ptr|
		m_connection.initialize(ptr);
		connected = true;
		m_ccb.value();
	}

	pvOnDisconnected {
		connected = false;
		m_dcb.value();
		m_connection = nil;
	}

	// CALLBACKS -----------------------------

	onTextMessageReceived_ { |callback|
		m_connection.onTextMessageReceived_(callback);
	}

	onBinaryMessageReceived_ { |callback|
		m_connection.onBinaryMessageReceived_(callback);
	}

	onOscMessageReceived_ { |callback|
		m_connection.onOscMessageReceived_(callback);
	}

	onHttpReplyReceived_ { |callback|
		m_http_cb = callback;
	}

	pvOnHttpReplyReceived { |ptr|
		var reply = HttpRequest.newFromPrimitive(ptr);
		m_http_cb.value(reply);
	}

	// WRITING -------------------------------

	writeText { |msg|
		m_connection.writeText(msg);
	}

	writeOsc { |...array|
		m_connection.writeOscClient(array);
	}

	writeBinary { |data|
		m_connection.writeBinary(data);
	}

	request { |req|
		_WebSocketClientRequest
		^this.primitiveFailed
	}

	free {
		g_instances.remove(this);
		m_connection = nil;
		this.primFree();
	}

	primFree {
		_WebSocketClientFree
		^this.primitiveFailed
	}
}

Http
{
	*ok { ^200 }
	*notFound { ^404 }
}

HttpRequest
{
	var m_ptr;
	var <>uri;
	var <>query;
	var <>mime;
	var <>body;

	*newFromPrimitive { |ptr|
		^this.newCopyArgs(ptr).reqCtor()
	}

	reqCtor {
		_HttpRequestBind
		^this.primitiveFailed
	}

	*new { |uri = '/', query = "", mime = "", body = ""|
		^this.newCopyArgs(0x0, uri, query, mime, body)
	}

	reply { |code, text, mime = ""|
		_HttpReply
		^this.primitiveFailed
	}

	replyJson { |json|
		// we assume code is 200 here
		this.reply(200, json, "application/json");
	}
}

WebSocketServer
{
	var m_ptr;
	var m_port;
	var m_name;
	var m_type;
	var m_connections;
	var m_ncb;
	var m_dcb;
	var m_hcb;

	classvar g_instances;

	*initClass {
		g_instances = [];
		ShutDown.add({
			postln("TCP-cleanup");
			g_instances.do(_.free());
		})
	}

	*new { |port, zname = "supercollider", ztype = "_oscjson._tcp"|
		^this.newCopyArgs(0x0, port, zname, ztype).wsServerCtor();
	}

	wsServerCtor {
		m_connections = [];
		g_instances = g_instances.add(this);
		this.prmInstantiateRun(m_port, m_name, m_type);
	}

	prmInstantiateRun { |port, name, type|
		_WebSocketServerInstantiateRun
		^this.primitiveFailed
	}

	at { |index|
		^m_connections[index];
	}

	writeAll { |data|
		m_connections.do(_.write(data));
	}

	onNewConnection_ { |callback|
		m_ncb = callback;
	}

	onDisconnection_ { |callback|
		m_dcb = callback;
	}

	onHttpRequestReceived_ { |callback|
		m_hcb = callback;
	}

	pvOnNewConnection { |con|
		var connection = WebSocketConnection.newInit(con);
		m_connections = m_connections.add(connection);
		m_ncb.value(connection)
	}

	pvOnHttpRequestReceived { |request|
		var screq = HttpRequest.newFromPrimitive(request);
		m_hcb.value(screq);
	}

	pvOnDisconnection { |connection|

	}

	free {
		g_instances.remove(this);
		this.prmFree();
	}

	prmFree {
		_WebSocketServerFree
		^this.primitiveFailed
	}
}



