interface Env {
  RTC_FW_DO_WORKER: DurableObjectNamespace;
}

// Worker
export async function onRequest(ctx: EventContext<Env, string, Record<string, unknown>>): Promise<Response> {
	const request = ctx.request;
	const env = ctx.env;


	let url = new URL(request.url);
	if (url.pathname == '/websocket') {
		// Expect to receive a WebSocket Upgrade request.
		// If there is one, accept the request and return a WebSocket Response.
		const upgradeHeader = request.headers.get('Upgrade');
		if (!upgradeHeader || upgradeHeader !== 'websocket') {
			console.log("Not a websocket conn");
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
		let offer = url.searchParams.get("offer");

		const jointHeaders = new Headers(request.headers);

		let added_headers = { code, offer };
		jointHeaders.set(
			"XF_FORWARDED_DATA",
			JSON.stringify(added_headers)
		);

		const newRequest = new Request(request, {headers: jointHeaders});

		let stub = ctx.env.RTC_FW_DO_WORKER.getByName("DO_MAIN_INSTANCE");
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
