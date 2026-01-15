#include <lib.h>
#include <main.hpp>

static gamescopeWebrtcCtx int_gamescopeWebrtc_INIT(void) {
    gamescopeWebrtcCtx* allocated_ctx = (gamescopeWebrtcCtx*) calloc(1, sizeof(gamescopeWebrtcCtx));
    allocated_ctx->test 
}


extern "C" gamescopeWebrtcCtx gamescopeWebrtc_INIT(void) {
    return int_gamescopeWebrtc_INIT();
} 