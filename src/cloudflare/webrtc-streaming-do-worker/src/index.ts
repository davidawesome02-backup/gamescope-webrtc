import { DurableObject } from 'cloudflare:workers';


const CODE_LENGTH = 6;

function random_b32(): string {
	let out = "";
	let alph = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
	const random_arr = new Uint32Array(CODE_LENGTH);
	crypto.getRandomValues(random_arr);
	for (let random of random_arr) out+=alph[random%alph.length];
	return out;
}

function normalize_b32(b32_raw: string): string {
	b32_raw = b32_raw.toUpperCase()
					.replaceAll("O","0")
					.replaceAll("I","1")
					.replaceAll("L","1");
	let alph = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"

	let out = "";
	for (let char of b32_raw) if (alph.includes(char)) out+=char;
	
	return out;
}

type WS_ATTACHMENT = {
    client_id: string;
    is_server: boolean;
    code: string | null;
};

// Durable Object
export class RtcForwardDO extends DurableObject {
	// Keeps track of all WebSocket connections
	// When the DO hibernates, gets reconstructed in the constructor
	// sessions: Map<WebSocket, { [key: string]: string }>;
	ws_known: WebSocket[];
	ws_attach: Map<WebSocket, WS_ATTACHMENT>;

	constructor(ctx: DurableObjectState, env: Env) {
		super(ctx, env);
		this.ws_attach = new Map();

		// As part of constructing the Durable Object,
		// we wake up any hibernating WebSockets and
		// place them back in the `sessions` map.

		// Get all WebSocket connections from the DO
		this.ws_known = this.ctx.getWebSockets();

		// Sets an application level auto response that does not wake hibernated WebSockets.
		this.ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair('ping', 'pong'));
	}

	// Quick cache map in case we get many look ups
	ws_get_attachment(ws: WebSocket): WS_ATTACHMENT | undefined {
		const stored_ws_attach = this.ws_attach.get(ws);
		if (stored_ws_attach) return stored_ws_attach;

		const ws_deserialized = ws.deserializeAttachment();
		this.ws_attach.set(ws, ws_deserialized)
		return ws_deserialized
	}

	async fetch(request: Request): Promise<Response> {
		// Creates two ends of a WebSocket connection.
		const webSocketPair = new WebSocketPair();
		const [client_ws_dont_use, server] = Object.values(webSocketPair);

		// Calling `acceptWebSocket()` informs the runtime that this WebSocket is to begin terminating
		// request within the Durable Object. It has the effect of "accepting" the connection,
		// and allowing the WebSocket to send and receive messages.
		// Unlike `ws.accept()`, `this.ctx.acceptWebSocket(ws)` informs the Workers Runtime that the WebSocket
		// is "hibernatable", so the runtime does not need to pin this Durable Object to memory while
		// the connection is open. During periods of inactivity, the Durable Object can be evicted
		// from memory, but the WebSocket connection will remain open. If at some later point the
		// WebSocket receives a message, the runtime will recreate the Durable Object
		// (run the `constructor`) and deliver the message to the appropriate handler.
		this.ctx.acceptWebSocket(server);


		const sent_host_data = request.headers.get("XF_FORWARDED_DATA");
		if (!sent_host_data) return new Response(null, { status: 500 });
		const forwarded_data: {
			code: string | null;
			offer: string | null;
		} = JSON.parse(sent_host_data);
		
		const client_id = crypto.randomUUID();

		if (forwarded_data.code != null) {
			forwarded_data.code = normalize_b32(forwarded_data.code);
			if (forwarded_data.code.length != 6) forwarded_data.code = null;
		}

		let is_server = forwarded_data.code == null;

		if (is_server) {
			if (forwarded_data.offer) {
				server.send(JSON.stringify({"type": "error", "error": `Invalid code format or offer.`}));
				server.close();
				return new Response(null, {
					status: 101,
					webSocket: client_ws_dont_use,
				});
			}

			forwarded_data.code = random_b32();

			server.send(JSON.stringify({"type": "code", "code": forwarded_data.code}));
			console.log(`Created new code: ${forwarded_data.code}`);
		} else {
			if (!forwarded_data.offer) {
				server.send(JSON.stringify({"type": "error", "error": `No offer supplied: ${forwarded_data.code}`}));
				server.close();
				return new Response(null, {
					status: 101,
					webSocket: client_ws_dont_use,
				});
			}

			let ws_host_server = Array.from(this.ws_known).find((find_ws) => {
				const parsed_att = this.ws_get_attachment(find_ws);

				return find_ws.readyState == WebSocket.OPEN && parsed_att && parsed_att.is_server && parsed_att.code == forwarded_data.code
			});

			if (ws_host_server) {
				ws_host_server.send(
					JSON.stringify({
						"type": "offer",
						"offer": forwarded_data.offer,
						"client_id": client_id
					})
				);
			} else {
				console.log("Failed to find server: "+ forwarded_data.code);
				server.send(JSON.stringify({"type": "error", "error": "Failed to find server"}));
				server.close();
				return new Response(null, {
					status: 101,
					webSocket: client_ws_dont_use,
				});
			}
		}



		let session_data: WS_ATTACHMENT = {
			client_id,
			is_server,
			code: forwarded_data.code
		};

		server.serializeAttachment(session_data);
		this.ws_known.push(server);
		this.ws_attach.set(server, session_data);

		console.log(`Ws opened: ${JSON.stringify(session_data)}`);

		return new Response(null, {
			status: 101,
			webSocket: client_ws_dont_use,
		});
	}

	async webSocketMessage(ws: WebSocket, message: ArrayBuffer | string) {
		// Get the session associated with the WebSocket connection.
		const ws_attach = this.ws_get_attachment(ws);
		if (!ws_attach) { console.log("Unknown websockt state!"); ws.close(500); return; }

		if (!ws_attach.is_server) return;

		if (typeof message != "string") return;

		let response_message: {
			client_id: string | undefined,
			response: string | undefined
		} | undefined = undefined;

		try {
			response_message = JSON.parse(message);
		} catch {}


		if (!response_message || typeof response_message.client_id != "string" || typeof response_message.response != "string" || !response_message.response) {
			ws.send(JSON.stringify({"type": "error", "error": "Failed to parse request"}));
			ws.close();
			return;
		}

		let ws_client = Array.from(this.ws_known).find((ws) => {
			const temp_session_data = this.ws_get_attachment(ws);
			if (!temp_session_data) return false;
			
			if (temp_session_data.is_server) return false; // Only send to upstream
			if (temp_session_data.client_id != response_message.client_id) return false;

			return true;
		})

		if (ws_client) {
			ws_client.send(JSON.stringify({"type": "answer", "answer": response_message.response}));
			ws_client.close();
		} else {
			ws.send(JSON.stringify({"type": "client_error", "client_error": "Client dead", "client_id": response_message.client_id}));
			ws.close();
		}
	}

	async webSocketClose(ws: WebSocket, code: number, reason: string, wasClean: boolean) {
		// If the client closes the connection, the runtime will invoke the webSocketClose() handler.
		console.log(`Ws closed: ${JSON.stringify(this.ws_get_attachment(ws))}`);
		this.ws_attach.delete(ws);

		const ws_index = this.ws_known.indexOf(ws);
		if (ws_index >= 0) this.ws_known.splice(ws_index, 1);
		// try {
		// 	ws.close(500, "WebSocket close attempted.");
		// } catch {}
	}
}

export default {};
