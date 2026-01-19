#include <lib.h>
#include <main.hpp>

static gamescopeWebrtcCtx* int_gamescopeWebrtc_INIT(bool kbm, bool ctrl) {
    gamescopeWebrtcCtx* allocated_ctx = (gamescopeWebrtcCtx*) calloc(1, sizeof(gamescopeWebrtcCtx));
    stateData* internal_ctx = (stateData*) calloc(1, sizeof(stateData));
    allocated_ctx->opaque_internal_ctx = (void*) internal_ctx;
    // allocated_ctx->test 

    // internal_ctx.

    return allocated_ctx;
}

extern "C" gamescopeWebrtcCtx* gamescopeWebrtc_INIT(bool kbm, bool ctrl) {
    return int_gamescopeWebrtc_INIT(kbm, ctrl);
} 