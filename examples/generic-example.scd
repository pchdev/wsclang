(
w = WebSocketServer(5678, "supercollider", "_oscjson._tcp");
// instantiates and runs a websocket server
// the server should be zeroconf-visible (as 'supercollider') by client devices (with the type '_oscjson._tcp', which is part of the oscquery specification, set here as an example)
// see https://github.com/Vidvox/OSCQueryProposal

w.onNewConnection = { |con|
	// each time a new client connects to the server, a WebSocketConnection is created
	// and stored within the server object, until closed/disconnected
	// the object is also passed in this callback, for convenience
	// here, we set individual callbacks for text/osc message reception
	postln(format("[websocket-server] new connection from %:%", con.address, con.port));

	con.onTextMessageReceived = { |msg|
		postln(format("[websocket-server] new message from: %:%", con.address, con.port));
		postln(msg);
		// echo back the received message to the client
		con.writeText(msg);
	};

	con.onOscMessageReceived = { |array|
		// this is OSC over websocket, convenient for critical message reception
		// the array is of the same format as a standard OSC array sent from a NetAddr
		// array[0] being the method ('/foo/bar')
		// and array[1..n] the arguments
		postln(format("[websocket-server] new osc message from: %:%", con.address, con.port));
		postln(array);
	};
};

w.onDisconnection = { |con|
	postln(format("[websocket-server] client %:%: disconnected", con.address, con.port));
};

w.onHttpRequestReceived = { |req|
	// the websocket server keeps its http-server functionalities
	// meaning it can receive standard non-websocket http requests from browsers or other http clients
	// here, we set the callback for passing these HttpRequest objects

	postln("[http-server] request received");
	postln(format("[http-server] uri: %", req.uri));

	if (req.query.isEmpty().not()) {
		postln(format("[http-server] query: %", req.query));
	};

	if (req.uri == "/") {
		if (req.query == "HOST_INFO") {
			// another oscquery example
			req.replyJson("{ \"NAME\": \"supercollider\", \"OSC_PORT\": 1234, \"OSC_TRANSPORT\": \"UDP\"}");
		} {
			req.reply(Http.ok, "hello world!", "text/plain");
		}
	}
};
)

// you can try http requests from the browser:
"http://localhost:5678".openOS();
"http://localhost:5678/?HOST_INFO".openOS();

(
c = WebSocketClient();

c.onConnected = {
	// client connection callback
	postln("[websocket-client] connected!");

	// requests root and host_info (for oscquery)
	c.request("/?HOST_INFO");
	c.request("/");
};

c.onHttpReplyReceived = { |reply|
	postln(format("[http-client] reply from server for uri: %, %", reply.uri, reply.body));
};

c.onTextMessageReceived = { |msg|
	postln(format("[websocket-client] message from server: %", msg));
};

c.onOscMessageReceived = { |msg|
	postln(format("[websocket-client] osc message from server: %", msg));
};

c.connect("127.0.0.1", 5678)

)

c.writeText("owls are not what they seem");
c.writeOsc('/world', 32004, 32.4343, "hellooo");
