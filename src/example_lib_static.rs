
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::thread;

#[repr(C)]
#[derive(Debug, Clone, Copy)]
#[allow(non_snake_case)]
pub struct gamescope_webrtc_ctx {
    pub crl_path: *const c_char,
    pub kbm_path: *const c_char,

    pub ICE_offer: *const c_char,
    pub join_code: *const c_char,
    pub webrtc_connection_failed: bool,

    pub result_err: c_int,

    pub opaque_internal_ctx: *mut c_void,
}

extern "C" {
    fn gamescope_webrtc_init( // ALL NAMES ARE WRONG HERE FIX DAVID GPT GENERATED THEM LOL
        create_kbm: bool,
        create_ctrl: bool,
    ) -> *mut gamescope_webrtc_ctx;

    fn gamescope_webrtc_create_webrtc(
        ctx: *mut gamescope_webrtc_ctx,
        fps: c_int,
        create_code: bool,
        code_creation_url: *mut c_char,
    );

    fn gamescope_webrtc_check_webrtc(
        ctx: *mut gamescope_webrtc_ctx,
    );

    fn gamescope_webrtc_start_recording(
        ctx: *mut gamescope_webrtc_ctx,
        gamescope_pid: c_int,
    );
}



pub fn start_webrtc_stream() -> *mut gamescope_webrtc_ctx {
    println!("Attempting to start stream.");
    unsafe {
        let context: *mut gamescope_webrtc_ctx = gamescope_webrtc_init(true, false);
        gamescope_webrtc_create_webrtc(context, 60, true, CString::new("wss://webrtc-streaming-pages.pages.dev/websocket").expect("asd").as_ptr().cast_mut());

        context
    }
}

pub fn check_webrtc_stream_codes(context: *mut gamescope_webrtc_ctx) -> Option<String> {
    unsafe {
        gamescope_webrtc_check_webrtc(context);
        if !(*context).join_code.is_null() {
            if let Ok(a) = CStr::from_ptr((*context).join_code).to_str() {
                return Some(a.to_string());
            }
        }
        None
    }
}

pub fn check_webrtc_stream_created_devices(context: *mut gamescope_webrtc_ctx) -> Option<String> {
    unsafe {
        if !(*context).kbm_path.is_null() {
            if let Ok(a) = CStr::from_ptr((*context).kbm_path).to_str() {
                return Some(a.to_string());
            }
        }
        None
    }
}



pub fn start_webrtc_streaming_thread(context: *mut gamescope_webrtc_ctx, pid: u32) -> thread::JoinHandle<()> {
    let ctx_adr = context as usize;
    thread::spawn(move || {
        unsafe {
            std::thread::sleep(std::time::Duration::from_secs_f64(5.0));
            let context = ctx_adr as *mut gamescope_webrtc_ctx;
            gamescope_webrtc_start_recording(context, pid as i32);
        }
    })
}