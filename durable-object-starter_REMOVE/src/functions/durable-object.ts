import { DurableObject } from 'cloudflare:workers';

const TTL_MS = 20 * 60 * 1000; // 30 minutes


// Durable Object
export class MyDurableObject extends DurableObject {
	// Keeps track of all WebSocket connections
	// When the DO hibernates, gets reconstructed in the constructor
	sessions: Map<WebSocket, { [key: string]: string }>;

	constructor(ctx: DurableObjectState, env: Env) {
		super(ctx, env);
		this.sessions = new Map();

		// As part of constructing the Durable Object,
		// we wake up any hibernating WebSockets and
		// place them back in the `sessions` map.

		// Get all WebSocket connections from the DO
		this.ctx.getWebSockets().forEach((ws) => {
		let attachment = ws.deserializeAttachment();
		if (attachment) {
			// If we previously attached state to our WebSocket,
			// let's add it to `sessions` map to restore the state of the connection.
			this.sessions.set(ws, { ...attachment });
		}
		});

		// Sets an application level auto response that does not wake hibernated WebSockets.
		this.ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair('ping', 'pong'));
	}

	async fetch(request: Request): Promise<Response> {
		// Creates two ends of a WebSocket connection.
		const webSocketPair = new WebSocketPair();
		const [client, server] = Object.values(webSocketPair);

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

		let forwareded_data: {
			code: string;
			is_opener: boolean;
			offer: string | null;
		} = JSON.parse(request.headers.get("XF_FORWARDED_DATA")!);
		
		if (forwareded_data.is_opener) {
			await this.ctx.storage.put({
				"initalized": true,
				"connection_code": forwareded_data.code,
				"offer": forwareded_data.offer
			});

			await this.ctx.storage.setAlarm(Date.now()+TTL_MS);

			server.send(JSON.stringify({"type": "code", code: forwareded_data.code}));
			console.log(`Created new code: ${forwareded_data.code}`)
		} else {
			if (!await this.ctx.storage.get("initalized")) {
				return new Response('This code is not valid!', {
					status: 400,
				});
			}
			let offer = await this.ctx.storage.get("offer");
			if (!offer) {
				return new Response('Offer not found!', {
					status: 400,
				});
			}

			server.send(JSON.stringify({ "type":"offer", offer }));
			console.log(`Sent client offer for code: ${forwareded_data.code}`)
		}


		const client_id = crypto.randomUUID(); // Honestly not needed, just keeping arround for testing.

		let session_data_raw = {
			client_id,
			is_opener: forwareded_data.is_opener
		};
		let session_data = JSON.stringify(session_data_raw);

		server.serializeAttachment({session_data});
		this.sessions.set(server, {session_data});

		

		return new Response(null, {
			status: 101,
			webSocket: client,
		});
	}

	async webSocketMessage(ws: WebSocket, message: ArrayBuffer | string) {
		// Get the session associated with the WebSocket connection.
		const session = this.sessions.get(ws)!;
		const session_data: {
			client_id: string;
			is_opener: boolean;
		} = JSON.parse(session.session_data);

		if (session_data.is_opener) return; // Offer should not send a message


		this.sessions.forEach((attachment, connectedWs) => {
			const temp_session_data: {
				client_id: string;
				is_opener: boolean;
			} = JSON.parse(attachment.session_data);
			
			if (!temp_session_data.is_opener) return; // Only send to upstream

			connectedWs.send(JSON.stringify({
				"type": "accept",
				"accept": message
			}));
		});

		// ws.close();
		this.cleanup("Done forwarding!");
	}

	async webSocketClose(ws: WebSocket, code: number, reason: string, wasClean: boolean) {
		// If the client closes the connection, the runtime will invoke the webSocketClose() handler.
		this.sessions.delete(ws);
		ws.close(code, 'Durable Object is closing WebSocket');
		console.log("Ws closed!")
	}

	async alarm() {
		await this.cleanup("TTL expired");
	}

	async cleanup(reason: string) {
		let connection_code = await this.ctx.storage.get('connection_code');
		console.log(`Cleaning up DO (${connection_code}): ${reason}`);

		// Close all sockets
		for (const ws of this.sessions.keys()) {
			try {
			ws.close(1000, "Session ended");
			} catch {}
		}

		this.sessions.clear();

		// Delete ALL persisted storage (this is what stops billing)
		await this.ctx.storage.deleteAll();
		await this.ctx.storage.deleteAlarm();
	}


}
