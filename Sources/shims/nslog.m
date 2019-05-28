#import <Foundation/Foundation.h>
#import "aah.h"
#import "printf.h"

SHIMDEF(NSLog) {
    // in apple's arm64, all variadic arguments are allocated 8-byte stack slots
    // that means we still need to know all the arguments in order to forward the call
    // also the format must be modified if it uses long doubles, since they're smaller on arm64
    
    // decode/modify format
    NSString *format = (NSString*)ctx->arm64_call_context->x[0];
    NSUInteger formatLength = [format lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    char *fmt = alloca(formatLength+1);
    [format getCString:fmt maxLength:formatLength+1 encoding:NSUTF8StringEncoding];
    int nargs = CountStringFormatArgs(fmt);
    if (nargs < 0) abort();
    char argEncoding[3+nargs];
    argEncoding[0] = 'v';
    argEncoding[1] = '*';
    size_t origFormatLength = strlen(fmt);
    EncodeStringFormatArgs(fmt, argEncoding+2, 1, 1);
    BOOL formatWasModified = strlen(fmt) != origFormatLength;
    
    // construct call
    ffi_cif cif_native;
    ffi_cif_arm64 cif_arm64;
    prep_cifs(&cif_native, &cif_arm64, argEncoding, 1);
    ctx->cif_native = &cif_native;
    ctx->cif_arm64 = &cif_arm64;
    NSString *newFormat = formatWasModified ? [[NSString alloc] initWithUTF8String:fmt] : format;
    ctx->arm64_call_context->x[0] = (uint64_t)newFormat;
    ctx->before = ctx->after = NULL;
    call_native_with_context(uc, ctx);
    if (newFormat != format) {
        [newFormat release];
    }
    free(cif_native.arg_types); // cif_arm64.arg_types is the same
    return SHIM_RETURN;
}
