# wsclang

A tester repository for a sclang websocket primitives proposal, using sc3.10 and mongoose library at the moment. This project was initially created as a replacement for the [libossia-supercollider](https://github.com/OSSIA/ossia-supercollider) bindings, and is currently used and developed in combination with @thibaudk's new [OSSIA quark](https://github.com/OSSIA/ossia-sclang) for supercollider.

## installing

- recursive clone this repository (`git clone --recursive https://github/com/pchdev/wsclang.git`)
- cd into it
- modify/run build.sh

***note:** the script will build and install a (slightly) modified version of SuperCollider. For convenience, we do not keep track of all recent sc updates and bugfixes, and only update the dependency occasionally. Keep in mind that your current (and probably newer) installation of SuperCollider will be overwritten*.

Furthermore, although we're focusing right now on Linux, we will do our best to test it and maintain it on macOS and Windows as well in the future.

## usage example

```js
(
w = WebSocketServer(5678);
z = ZeroconfService("supercollider", "_oscjson._tcp", w.port);
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
b = ZeroconfBrowser("_oscjson._tcp", "supercollider", { |target|
	postln(format("[zeroconf] target resolved: % (%) at address: %:%",
		target.name, target.domain, target.address, target.port));
	target.onDisconnected = {
		postln(format("[zeroconf] target % is now offline", target.name));
	};
	// when our target 'supercollider' (our websocket server) is online and resolved
	// through zeroconf, automatically connect the client to it from its address and port.
	c.connect(target.address, target.port)
});

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

)

c.writeText("owls are not what they seem");
c.writeOsc('/world', 32004, 32.4343, "hellooo");
w[0].writeOsc("/world", 32001, 32.66, "two coopers");

```
