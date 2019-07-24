#import <Foundation/Foundation.h>
#import "aah.h"

// shims for common foundation methods

size_t NumberOfNonNullVarArgs(void ** stack) {
    // arm64 varargs are always on stack
    size_t arg = 0;
    while(stack[arg] != NULL) {
        arg++;
    }
    return arg;
}

// obj-c call with nil-terminated list of objects:
// initWithObjectsAndKeys:firstObject,...

SHIMDEF(nilTerminatedListOfObjects) {
    size_t args = NumberOfNonNullVarArgs((void**)ctx->sp);
    char *argEncoding = alloca(3 + args + 3);
    memset(argEncoding, '@', 3 + args + 2);
    argEncoding[2] = ':';
    argEncoding[3 + args + 2] = '\0';
    printf("calling with arg encoding %s\n", argEncoding);
    // construct call
    ffi_cif cif_native;
    ffi_cif_arm64 cif_arm64;
    prep_cifs(&cif_native, &cif_arm64, argEncoding, 3);
    ctx->cif_native = &cif_native;
    ctx->cif_arm64 = &cif_arm64;
    ctx->before = ctx->after = NULL;
    call_native_with_context(uc, ctx);
    free(cif_native.arg_types); // cif_arm64.arg_types is the same*/
    return SHIM_RETURN;
}

// return nil
SHIMDEF(nop) {
    ctx->arm64_call_context->x[0] = 0;
    return SHIM_RETURN;
}
