import { MyDurableObject } from "./durable-object";
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
					 .replaceAll("O","0")
					 .replaceAll("i","1")
					 .replaceAll("l","1")
					 .replaceAll("s","5");
	let alph = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"

	let out = "";
	for (let char of b32_raw) if (alph.includes(char)) out+=char;
	
	return out;
}

// // Worker
// export default {
// 	async fetch(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
// 		let url = new URL(request.url);
// 		if (url.pathname == '/websocket') {
// 			// Expect to receive a WebSocket Upgrade request.
// 			// If there is one, accept the request and return a WebSocket Response.
// 			const upgradeHeader = request.headers.get('Upgrade');
// 			if (!upgradeHeader || upgradeHeader !== 'websocket') {
// 				return new Response('Worker expected Upgrade: websocket', {
// 				status: 426,
// 				});
// 			}

// 			if (request.method !== 'GET') {
// 				return new Response('Worker expected GET method', {
// 				status: 400,
// 				});
// 			}

// 			const url = new URL(request.url)
// 			let code = url.searchParams.get("code")
// 			let is_opener = false;

// 			let offer = url.searchParams.get("offer");

// 			// Client A: create new session
// 			if (!code) {
// 				if (!offer) {
// 					return new Response('No offer found for opening request!', {
// 						status: 400,
// 					});
// 				}
// 				code = random_b32();
// 				is_opener = true;
// 			} else {
// 				code = normalize_b32(code);
// 				if (code.length != CODE_LENGTH) {
// 					return new Response('Invalid code.', {
// 						status: 400,
// 					});
// 				}
// 			}

// 			let stub = env.MY_DURABLE_OBJECT.getByName("FF_CODE_"+code);

// 			const jointHeaders = new Headers(request.headers);
// 			let added_headers = { code, is_opener, offer };
// 			jointHeaders.set(
// 				"XF_FORWARDED_DATA",
// 				JSON.stringify(added_headers)
// 			);

// 			const newRequest = new Request(request, {headers: jointHeaders});

// 			return stub.fetch(newRequest);
// 		}

// 		return new Response(
// 		`Supported endpoints:
// 	/websocket: Expects a WebSocket upgrade request`,
// 		{
// 			status: 200,
// 			headers: {
// 			'Content-Type': 'text/plain',
// 			},
// 		}
// 		);
// 	},
// };

// Worker
export async function onRequest(ctx): Promise<Response> {
	const request = ctx.request;
	const env = ctx.env;


	let url = new URL(request.url);
	if (url.pathname == '/websocket') {
		// Expect to receive a WebSocket Upgrade request.
		// If there is one, accept the request and return a WebSocket Response.
		const upgradeHeader = request.headers.get('Upgrade');
		if (!upgradeHeader || upgradeHeader !== 'websocket') {
			return new Response('Worker expected Upgrade: websocket', {
			status: 426,
			});
		}

		if (request.method !== 'GET') {
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
				return new Response('No offer found for opening request!', {
					status: 400,
				});
			}
			code = random_b32();
			is_opener = true;
		} else {
			code = normalize_b32(code);
			if (code.length != CODE_LENGTH) {
				return new Response('Invalid code.', {
					status: 400,
				});
			}
		}

		let stub = ctx.env.MY_DURABLE_OBJECT.getByName("FF_CODE_"+code);

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

export { MyDurableObject };