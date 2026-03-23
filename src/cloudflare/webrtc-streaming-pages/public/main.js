/** @type {RTCDataChannel} */
let data_channel;

const videoElement = document.getElementById('video-element');
videoElement.onload = () => {
    console.log("Video loaded!")
}

function getVideoCoords(video, event) {
    const rect = video.getBoundingClientRect();
    const vw = video.videoWidth;
    const vh = video.videoHeight;

    const scaleX = rect.width / vw;
    const scaleY = rect.height / vh;
    const scale = Math.min(scaleX, scaleY);

    const displayW = vw * scale;
    const displayH = vh * scale;

    const offsetX = (rect.width  - displayW) / 2;
    const offsetY = (rect.height - displayH) / 2;

    const x = event.clientX - rect.left - offsetX;
    const y = event.clientY - rect.top  - offsetY;

    if (x < 0 || y < 0 || x > displayW || y > displayH) return null;

    return { x: x / scale, y: y / scale };
}

videoElement.onclick = (evt) => {
    if (!document.pointerLockElement) {
        videoElement.requestPointerLock();
    }
};

function lockChangeAlert() {
    if (document.pointerLockElement === videoElement) {
        console.log("The pointer lock status is now locked");
        document.addEventListener("mousemove", updateMousePosition);
        document.addEventListener("mouseup", updateMousePosition);
        document.addEventListener("mousedown", updateMousePosition);
        document.addEventListener("wheel", updateMouseWheel);
        document.addEventListener("keydown", updateKeyPresses_D);
        document.addEventListener("keyup", updateKeyPresses_U);
        try {navigator?.keyboard?.lock?.()} catch {}

        toggleAllButtonsOff();
    } else {
        console.log("The pointer lock status is now unlocked");
        document.removeEventListener("mousemove", updateMousePosition);
        document.removeEventListener("mouseup", updateMousePosition);
        document.removeEventListener("mousedown", updateMousePosition);
        document.removeEventListener("wheel", updateMouseWheel);
        document.removeEventListener("keydown", updateKeyPresses_D);
        document.removeEventListener("keyup", updateKeyPresses_U);
        try {navigator?.keyboard?.unlock?.()} catch {}

        toggleAllButtonsOff();
        setTimeout(toggleAllButtonsOff, 10); // Just catch any stranglers that may have happened from detach.
    }
}
const device_change_info = {
    // X, Y, Scroll
    "delta_devices": [0, 0, 0],
    "previous_buttons_status":  {},
    "change_buttons_status":    {},
    "current_buttons_status":   {},
}
window.device_change_info = device_change_info;

function toggleAllButtonsOff() {
    for (i in device_change_info.previous_buttons_status) {
        if (device_change_info.previous_buttons_status[i]!=true) continue; // If the device is pressed down, press it up
        device_change_info.change_buttons_status[i]=true;
        device_change_info.current_buttons_status[i]=false;
    }
    publishRemoteDeviceUpdates();
}

function updateMousePosition(e) {
    // Mouse movement and held buttons
    device_change_info["delta_devices"][0] += e.movementX;
    device_change_info["delta_devices"][1] += e.movementY;

    updateKeyPresses("MOUSE_LEFT",   (e.buttons>>0) & 1 >0)
    updateKeyPresses("MOUSE_RIGHT",  (e.buttons>>1) & 1 >0)
    updateKeyPresses("MOUSE_MIDDLE", (e.buttons>>2) & 1 >0)

    e?.preventDefault?.();
    return false;
}
function updateMouseWheel(e) {
    // Mouse movement and held buttons
    device_change_info["delta_devices"][2] += -e.deltaY;
}

function updateKeyPresses_U(e) {updateKeyPresses(e.code, false); e?.preventDefault?.(); return false;}
function updateKeyPresses_D(e) {updateKeyPresses(e.code, true); e?.preventDefault?.(); return false;}
function updateKeyPresses(c, pressed) {
    if (!window?.key_mapping?.[c]) return;
    if (device_change_info.current_buttons_status[c] == pressed) return;

    device_change_info.current_buttons_status[c] = pressed;
    device_change_info.change_buttons_status[c]  = true;
}

function publishRemoteDeviceUpdates() {
    if (data_channel && data_channel?.readyState !== "open") return;
    if (
        device_change_info["delta_devices"][0] == 0 &&
        device_change_info["delta_devices"][1] == 0 &&
        device_change_info["delta_devices"][2] == 0 &&
        Object.keys(device_change_info.change_buttons_status).length == 0
    ) return;

    let data_buffer = new ArrayBuffer(1024);
    let data = new DataView(data_buffer);
    let current_len = 0;

    let add_uint8_t = (value) => {data.setUint8(current_len, value); current_len+=1}
    let add_int16_t = (value) => {data.setInt16(current_len, value, true); current_len+=2}
    let add_uint16_t = (value) => {data.setUint16(current_len, value, true); current_len+=2}

    add_uint8_t(0) // version
    add_int16_t(device_change_info["delta_devices"][0]) // X Change
    add_int16_t(device_change_info["delta_devices"][1]) // Y Change
    add_int16_t(device_change_info["delta_devices"][2]) // Scroll Change



    for (changed_button in device_change_info.change_buttons_status) {
        let current_status = device_change_info?.current_buttons_status?.[changed_button];
        let previous_status = device_change_info?.previous_buttons_status?.[changed_button]

        let new_held_status = 
            !(
                previous_status ?? // Use current status to toggle if it exists
                !current_status // Or fall back to current one (using ! so it toggles twice)
            );

        device_change_info.previous_buttons_status[changed_button] = new_held_status;

        if (new_held_status == !!current_status) delete device_change_info.change_buttons_status[changed_button];

        let button_byte = (window?.key_mapping?.[changed_button] & 0x0fff);
        let options_byte = ((new_held_status?1:0)<<12) & 0xf000;

        add_uint16_t(button_byte | options_byte)
    }

    data_channel.send(new Uint8Array(data_buffer,0,current_len))


    device_change_info["delta_devices"][0] = 0;
    device_change_info["delta_devices"][1] = 0;
    device_change_info["delta_devices"][2] = 0;
}

setInterval(publishRemoteDeviceUpdates, 1000/60);
document.addEventListener("pointerlockchange", lockChangeAlert);


function b32dec(input) {
    // RFC 4648 Base32 character set
    const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
    // Map characters to values
    const map = Object.create(null);
    for (let i = 0; i < alphabet.length; ++i)
        map[alphabet[i]] = i;
    // Also support lowercase
    for (let i = 0; i < alphabet.length; ++i)
        map[alphabet[i].toLowerCase()] = i;

    // Remove any chars not in alphabet (not strictly necessary)
    input = input.replace(/[^A-Za-z2-7]/g, '');

    let bits = 0;
    let value = 0;
    let output = '';

    for (let i = 0; i < input.length; ++i) {
        value = (value << 5) | map[input[i]];
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            output += String.fromCharCode((value >> bits) & 0xFF);
            value = value & ((1 << bits) - 1);
        }
    }

    return output;
}



document.querySelector('button').addEventListener('click',  async () => {
    const code_short = document.getElementById('input_code').value;
    const ws_url = new URL(window.location);
    ws_url.pathname="/websocket";
    ws_url.searchParams.set("code",code_short);

    const websock = new WebSocket(ws_url);
    websock.onclose = (e) => {
        console.log("Disconnected from webscoket: ", e);
        document.getElementById('input_code').value = "";
    }
    websock.onmessage = async (e) => {
        websock_msg = JSON.parse(e.data)
        if (websock_msg.type != "offer") return;
        console.log("WS response recived, generating local, and waiting for remote")
        

        const pc = new RTCPeerConnection({
            // Recommended for libdatachannel
            bundlePolicy: 'max-bundle',
            iceServers: [ { urls: "stun:stun.l.google.com:19302" }, { urls: "stun:stun1.l.google.com:19302" }, { urls: "stun:stun2.l.google.com:19302" } ]
        });

        pc.onicegatheringstatechange = (state) => {
            if (pc.iceGatheringState === 'complete') {
                // We only want to provide an answer once all of our candidates have been added to the SDP.
                const answer = pc.localDescription;
                let webrtc_encoded_answer = JSON.stringify({"type": answer.type, sdp: answer.sdp});
                
                websock.send(webrtc_encoded_answer);
                console.log("Sending encoded answer:", webrtc_encoded_answer);
            }
        }

        pc.ontrack = (evt) => {
            console.log("Got webrtc track: ", evt);

            evt.streams[0].onactive = () => {
                console.log("Webrtc track active!")
            }
            evt.streams[0].onaddtrack = () => {
                console.log("Webrtc new track added.")
            }

            videoElement.srcObject = evt.streams[0];
            try {
                videoElement.play();
            } catch {console.log("Cant autoplay new track!")}

            // Try setting jitter buffer parameters, none of this work :P so I have to just send at 60hz on server.
            const receiver = evt.transceiver.receiver;
            receiver.jitterBufferTarget = 0; // Lowering jitter buffer target
            receiver.jitterBufferDelayHint = 0; // Lowering jitter buffer delay hint
            receiver.playoutDelayHint = 0; // Minimizing playout delay
        };
        pc.ondatachannel = (evt) => {
            console.log("Data channel starting!");
            data_channel = evt.channel;
            evt.channel.addEventListener("open", ()=>{
                console.log("Datachannel open, enabling input and playback!");
                
                document.getElementById("connect_popup").classList.toggle("hidden", true);
                document.getElementById("video-element").classList.toggle("hidden", false);
            })
            evt.channel.addEventListener("message", (e)=>{
                console.log("Datachannel sent message: ", e);
                try {
                    parsed_message = JSON.parse(e.data);
                    switch (parsed_message?.["type"]) {
                        case "close":
                            pc.close() // DOES NOT TRIGGER onconnectionstatechange
                            pc_on_close()
                            break;
                        default:
                            break;
                    }
                } catch {}
            });
        }

        pc.onconnectionstatechange = (evt) => {
            switch (pc.connectionState) {
                case "disconnected":
                case "closed":
                case "failed":
                    pc_on_close()
                default:
                    break;
            }
        }

        let decoded_b64_offer = JSON.parse(b32dec(websock_msg.offer));
        console.log("Websocket offered: ",decoded_b64_offer);

        await pc.setRemoteDescription(decoded_b64_offer);


        const answer = await pc.createAnswer();

        console.log("Setting local webrtc answer: ", answer);
        await pc.setLocalDescription(answer);

        // For debugging purposes
        window.pc_ = pc
    }
})

function pc_on_close() {
    document?.getElementById?.("connect_popup")?.classList?.toggle?.("hidden", false);
    videoElement?.classList?.toggle?.("hidden", true);
    document?.exitFullscreen?.();
    document?.exitPointerLock?.();
}