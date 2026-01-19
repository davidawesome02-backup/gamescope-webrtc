/** @type {RTCDataChannel} */
let data_channel;

// document.querySelector('textarea').value = String.raw`PASTE HERE`;
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
    // console.log(getVideoCoords(videoElement, evt))
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
    // send_data = new Int8Array([0, e.movementX, e.movementY, 0, e.buttons]);
    // data_channel.send(send_data)
    updateKeyPresses("MOUSE_LEFT",   (e.buttons>>0) & 1 >0)
    updateKeyPresses("MOUSE_RIGHT",  (e.buttons>>1) & 1 >0)
    updateKeyPresses("MOUSE_MIDDLE", (e.buttons>>2) & 1 >0)

    e?.preventDefault?.();
    return false;
}
function updateMouseWheel(e) {
    // Mouse movement and held buttons
    // send_data = new Int8Array([0, 0, 0, -e.deltaY/120, 0]);
    device_change_info["delta_devices"][2] += -e.deltaY;
    
    // data_channel.send(send_data)
}
function updateKeyPresses_U(e) {updateKeyPresses(e.code, false); e?.preventDefault?.(); return false;}
function updateKeyPresses_D(e) {updateKeyPresses(e.code, true); e?.preventDefault?.(); return false;}
function updateKeyPresses(c, pressed) {
    if (!window?.key_mapping?.[c]) return;
    if (device_change_info.current_buttons_status[c] == pressed) return;
    // console.log(c, pressed)

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

    // int16_to_int8 = (i) => {
    //     return [i&0xff, (i>>8)&0xff]
    // }

    // send_data = new Uint8Array([
    //     0, // Type of message (0 is only supported currently)
    //     int16_to_int8(device_change_info["delta_devices"][0]), // X Change
    //     int16_to_int8(device_change_info["delta_devices"][1]), // Y Change
    //     int16_to_int8(device_change_info["delta_devices"][2]), // Scroll change
    //     0
    // ].flat());

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

        let temp_new_status = 
            !(
                previous_status ?? // Use current status to toggle if it exists
                !current_status // Or fall back to current one (using ! so it toggles twice)
            );

        device_change_info.previous_buttons_status[changed_button] = temp_new_status;

        if (temp_new_status == !!current_status) delete device_change_info.change_buttons_status[changed_button];

        add_uint16_t((window?.key_mapping?.[changed_button] & 0x0fff) | (temp_new_status?0x1000:0))
        // console.log(temp_new_status, changed_button)
    }

    // console.log(new Uint8Array(data_buffer,0,current_len))

    data_channel.send(new Uint8Array(data_buffer,0,current_len))


    device_change_info["delta_devices"][0] = 0;
    device_change_info["delta_devices"][1] = 0;
    device_change_info["delta_devices"][2] = 0;
}

setInterval(publishRemoteDeviceUpdates, 1000/60);
document.addEventListener("pointerlockchange", lockChangeAlert);


// function b32dec(input) {
//     const T = new Int8Array(256).fill(-1);

//     for (let i = 0; i < 26; i++) {
//         T[65 + i] = T[97 + i] = i;      // A-Z a-z
//     }
//     for (let i = 0; i < 6; i++) {
//         T[50 + i] = 26 + i;            // 2-7
//     }

//     T[111] = T[79] = 0;  // o O
//     T[105] = T[73] = 1;  // i I
//     T[108] = T[76] = 1;  // l L
//     T[115] = T[83] = 5;  // s S

//     let out = "";
//     let buf = 0, bits = 0;

//     for (let i = 0; i < input.length; i++) {
//         const v = T[input.charCodeAt(i)];
//         if (v < 0) continue;
//         buf = (buf << 5) | v;
//         bits += 5;
//         if (bits >= 8) {
//             out += String.fromCharCode((buf >> (bits - 8)) & 0xff);
//             bits -= 8;
//         }
//     }
//     return out;
// }

// function b32dec(input) {
//     const T = new Int8Array(256).fill(-1);

//     for (let i = 0; i < 26; i++) {
//         T[65 + i] = i;  // A-Z
//         T[97 + i] = i;  // a-z
//     }
//     for (let i = 0; i < 6; i++) {
//         T[50 + i] = 26 + i; // 2-7
//     }

//     // Ambiguous character aliases
//     T[79]  = T[111] = 0; // O o → 0
//     T[73]  = T[105] = 1; // I i → 1
//     T[76]  = T[108] = 1; // L l → 1
//     T[83]  = T[115] = 5; // S s → 5

//     let out = [];
//     let buf = 0;
//     let bits = 0;

//     for (let i = 0; i < input.length; i++) {
//         const v = T[input.charCodeAt(i)];
//         if (v < 0) continue;

//         buf = (buf << 5) | v;
//         bits += 5;

//         if (bits >= 8) {
//             bits -= 8;
//             out.push((buf >> bits) & 0xff);
//             buf &= (1 << bits) - 1; // 🔑 discard consumed bits
//         }
//     }

//     return String.fromCharCode(...out);
// }

// function b32dec(input) {
//     // Prepare translation table (as in your current code)
//     const T = new Int8Array(256).fill(-1);

//     for (let i = 0; i < 26; i++) {
//         T[65 + i] = i;  // A-Z
//         T[97 + i] = i;  // a-z
//     }
//     for (let i = 0; i < 6; i++) {
//         T[50 + i] = 26 + i; // 2-7
//     }

//     // Ambiguous aliases
//     T[79]  = T[111] = 0; // O o → 0
//     T[73]  = T[105] = 1; // I i → 1
//     T[76]  = T[108] = 1; // L l → 1
//     T[83]  = T[115] = 5; // S s → 5

//     let out = [];
//     let buf = 0;
//     let bits = 0;

//     for (let i = 0; i < input.length; ++i) {
//         let ch = input.charCodeAt(i);
//         let val = T[ch];
//         if (val < 0) continue; // skip invalid
//         buf = (buf << 5) | val;
//         bits += 5;
//         // Output as many bytes as possible
//         while (bits >= 8) {
//             bits -= 8;
//             out.push((buf >> bits) & 0xff);
//             // Mask to only keep bits left, not needed but matches encoder style.
//             buf &= (1 << bits) - 1;
//         }
//     }
//     // Don't attempt to flush remaining bits! That's padding in encoding.

//     // Convert to string
//     return String.fromCharCode.apply(null, out);
// }

// function b32dec(input) {
//   const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
//   const map = Object.create(null);
//   for (let i = 0; i < alphabet.length; ++i) {
//     map[alphabet[i]] = i;
//   }
//   // Substitutions for o→0, i→1, l→1, s→5 (all uppercase)
//   const subs = { 'O': '0', 'I': '1', 'L': '1', 'S': '5' };

//   input = input.toUpperCase();
//   let normalized = '';
//   for (let j = 0; j < input.length; ++j) {
//     const c = input[j];
//     if (subs[c]) {
//       normalized += subs[c];
//     } else {
//       normalized += c;
//     }
//   }

//   let bits = 0;
//   let value = 0;
//   let output = '';

//   for (let i = 0; i < normalized.length; ++i) {
//     const c = normalized[i];
//     if (!(c in map)) continue; // skip any non-base32 char
//     value = (value << 5) | map[c];
//     bits += 5;
//     if (bits >= 8) {
//       bits -= 8;
//       output += String.fromCharCode((value >> bits) & 0xFF);
//       value = value & ((1 << bits) - 1);
//     }
//   }

//   return output;
// }
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
        console.log(e);
        document.getElementById('input_code').value = "";
    }
    websock.onmessage = async (e) => {
        websock_msg = JSON.parse(e.data)
        if (websock_msg.type != "offer") return;
        

        const pc = new RTCPeerConnection({
            // Recommended for libdatachannel
            bundlePolicy: 'max-bundle',
            iceServers: [ { urls: "stun:stun.l.google.com:19302" }, { urls: "stun:stun1.l.google.com:19302" }, { urls: "stun:stun2.l.google.com:19302" } ]
        });

        pc.onicegatheringstatechange = (state) => {
            if (pc.iceGatheringState === 'complete') {
                // We only want to provide an answer once all of our candidates have been added to the SDP.
                const answer = pc.localDescription;
                let asd = JSON.stringify({"type": answer.type, sdp: answer.sdp});
                
                websock.send(asd);
                console.log(asd);
                // document.querySelector('textarea').value = asd;
                // document.querySelector('p').value = 'Please paste the answer in the sender application.';
                // navigator.sendBeacon("test?a="+btoa(JSON.stringify({"type": answer.type, sdp: answer.sdp})) );
                // // alert('Please paste the answer in the sender application.');
            }
        }

        pc.ontrack = (evt) => {
            console.log(evt)
            console.log("Got track!", evt);
            evt.streams[0].onactive = () => {
                console.log("Playback!")
            }
            evt.streams[0].onaddtrack = () => {
                console.log("Playback 3!")
            }
            console.log("asdas: ", evt.streams[0].active)
            videoElement.srcObject = evt.streams[0];
            try {
            videoElement.play();
            } catch {console.log("Cant autoplay!")}
            // Try setting jitter buffer parameters, none of this work :P
            const receiver = evt.transceiver.receiver;
            receiver.jitterBufferTarget = 0; // Lowering jitter buffer target
            receiver.jitterBufferDelayHint = 0; // Lowering jitter buffer delay hint
            receiver.playoutDelayHint = 0; // Minimizing playout delay
        };
        pc.ondatachannel = (evt) => {
            console.log(evt);
            data_channel = evt.channel;
            evt.channel.addEventListener("open", ()=>{
                console.log("datach open");
                
                document.getElementById("connect_popup").classList.toggle("hidden");
                document.getElementById("video-element").classList.toggle("hidden");
                

            })
        }

        console.log(websock_msg);
        console.log(b32dec(websock_msg.offer))
        await pc.setRemoteDescription(JSON.parse(b32dec(websock_msg.offer)));

        const answer = await pc.createAnswer();
        await pc.setLocalDescription(answer);
        window.pc_ = pc
    }

    
})

