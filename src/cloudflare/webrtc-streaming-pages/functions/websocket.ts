// export const onRequest = async (context) => {
//   const id = context.env.RTC_FW_DO_WORKER.idFromName("some-room");
//   const stub = context.env.RTC_FW_DO_WORKER.get(id);

//   // Forward request to the Durable Object
//   return stub.fetch(context.request);
// };


// import { RtcForwardDO } from "./durable-object";

interface Env {
  RTC_FW_DO_WORKER: DurableObjectNamespace;
  // ...other bindings, if any
}

// import type { PagesFunction, PagesFunctionContext } from "@cloudflare/workers-types";

import type { PagesFunction } from "@cloudflare/workers-types";

const CODE_LENGTH = 6;



function random_b32(): string {
	let out = "";
	let alph = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
	const random_arr = new Uint32Array(CODE_LENGTH);
	crypto.getRandomValues(random_arr);
	for (let random of random_arr) out+=alph[random%alph.length];
	return out;
}
function normalize_b32(b32_raw: string): string {
	b32_raw = b32_raw.toUpperCase()
					 .replaceAll("0","O")
					 .replaceAll("1","i")
					//  .replaceAll("1","l")
					 .replaceAll("s","5");
	let alph = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"

	let out = "";
	for (let char of b32_raw) if (alph.includes(char)) out+=char;
	
	return out;
}


// Worker
// export async function onRequest(ctx: PagesFunction<Env>): Promise<Response> {
// export const onRequest: PagesFunction<Env> = async (ctx) => {
export async function onRequest(ctx: EventContext<Env, string, Record<string, unknown>>): Promise<Response> {
	const request = ctx.request;
	const env = ctx.env;


	let url = new URL(request.url);
	if (url.pathname == '/websocket') {
		// Expect to receive a WebSocket Upgrade request.
		// If there is one, accept the request and return a WebSocket Response.
		const upgradeHeader = request.headers.get('Upgrade');
		if (!upgradeHeader || upgradeHeader !== 'websocket') {
			console.log("Not a websocket con");
			return new Response('Worker expected Upgrade: websocket', {
			status: 426,
			});
		}

		if (request.method !== 'GET') {
			console.log("Method not GET");
			return new Response('Worker expected GET method', {
			status: 400,
			});
		}

		const url = new URL(request.url)
		let code = url.searchParams.get("code")
		let is_opener = false;

		let offer = url.searchParams.get("offer");

		// Client A: create new session
		if (!code) {
			if (!offer) {
				console.log("No offer for setting request: "+ code);
				return new Response('No offer found for opening request!', {
					status: 400,
				});
			}
			code = random_b32();
			is_opener = true;
		} else {
			code = normalize_b32(code);
			if (code.length != CODE_LENGTH) {
				console.log("Code too short: "+ code);
				return new Response('Invalid code.', {
					status: 400,
				});
			}
		}

		let stub = ctx.env.RTC_FW_DO_WORKER.getByName("FF_CODE_"+code);

		const jointHeaders = new Headers(request.headers);
		let added_headers = { code, is_opener, offer };
		jointHeaders.set(
			"XF_FORWARDED_DATA",
			JSON.stringify(added_headers)
		);

		const newRequest = new Request(request, {headers: jointHeaders});

		return stub.fetch(newRequest);
	}

	return new Response(
	`Supported endpoints:
/websocket: Expects a WebSocket upgrade request`,
	{
		status: 200,
		headers: {
		'Content-Type': 'text/plain',
		},
	}
	);
};
