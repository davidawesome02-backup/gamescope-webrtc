#ifndef MYLIB_H
#define MYLIB_H

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
    int test;
} gamescopeWebrtcCtx;

#ifdef __cplusplus
}
#endif

#endif // MYLIB_H
