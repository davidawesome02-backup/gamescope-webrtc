#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// // Opaque handle pattern
// typedef struct MyObject MyObject;

// // Constructor / destructor
// MyObject* myobject_new(int value);
// void myobject_free(MyObject* obj);

// // Methods
// int myobject_get_value(const MyObject* obj);
// void myobject_set_value(MyObject* obj, int value);

typedef struct {
    char* crl_path;
    char* kbm_path;

    void* opaque_internal_ctx;
} gamescopeWebrtcCtx;

gamescopeWebrtcCtx* gamescopeWebrtc_INIT(bool, bool);

#ifdef __cplusplus
}
#endif
