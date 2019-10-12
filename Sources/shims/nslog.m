#import <Foundation/Foundation.h>
#import "aah.h"
#import "printf.h"

// in apple's arm64, all variadic arguments are allocated 8-byte stack slots
// that means we still need to know all the arguments in order to forward the call
// also the format must be modified if it uses long doubles, since they're smaller on arm64
uint64_t generic_printf_shim(uc_engine *uc, struct native_call_context *ctx, const char *encoding) {
    // last element of encoding is format arg
    // format should only contain single-character encodings (eg no ^v)
    int format_arg = (int)strlen(encoding) - 2;
    char format_encoding = encoding[format_arg+1];
    int is_objc = (format_encoding == '@');
    if (format_encoding  != '@' && format_encoding != '*') {
        fprintf(stderr, "printf-like shim: invalid encoding \"%s\": format arg must be @ or *\n", encoding);
    }
    
    // decode/modify format
    void *format = (void*)ctx->arm64_call_context->x[format_arg];
    size_t formatLength = is_objc ? [(NSString*)format lengthOfBytesUsingEncoding:NSUTF8StringEncoding] : strlen(format);
    char *fmt = alloca(formatLength+1);
    if (is_objc) {
        [(NSString*)format getCString:fmt maxLength:formatLength+1 encoding:NSUTF8StringEncoding];
    } else {
        memcpy(fmt, format, formatLength+1);
    }
    int nargs = CountStringFormatArgs(fmt);
    if (nargs < 0) abort();
    char argEncoding[3+format_arg+nargs];
    memcpy(argEncoding, encoding, 2 + format_arg);
    size_t origFormatLength = strlen(fmt);
    EncodeStringFormatArgs(fmt, argEncoding+format_arg+2, is_objc, 1);
    BOOL formatWasModified = strlen(fmt) != origFormatLength;
    argEncoding[2+format_arg+nargs] = '\0';
    
    // construct call
    ffi_cif cif_native;
    ffi_cif_arm64 cif_arm64;
    prep_cifs(&cif_native, &cif_arm64, argEncoding, format_arg + 1);
    ctx->cif_native = &cif_native;
    ctx->cif_arm64 = &cif_arm64;
    NSString *newFormat = nil;
    if (formatWasModified) {
        if (is_objc) {
            newFormat = [[NSString alloc] initWithUTF8String:fmt];
            ctx->arm64_call_context->x[format_arg] = (uint64_t)newFormat;
        } else {
            ctx->arm64_call_context->x[format_arg] = (uint64_t)fmt;
        }
    }
    ctx->before = ctx->after = NULL;
    call_native_with_context(uc, ctx);
    if (newFormat) {
        [newFormat release];
    }
    free(cif_native.arg_types); // cif_arm64.arg_types is the same*/
    return SHIM_RETURN;
}

#define PRINTF_SHIM(_name, _format) SHIMDEF(_name) { return generic_printf_shim(uc, ctx, _format); }
#define VPRINTF_SHIM(_name, _function, _format) SHIMDEF(_name) { \
    _Static_assert(sizeof(_format) < 10, "vprintf shim should have 8 args or less"); \
    ctx->pc = (uint64_t)_function; /* call non-va_list function */ \
    ctx->sp = ctx->arm64_call_context->x[sizeof(_format)-2]; /* va_list is laid out like varargs on stack */ \
    return generic_printf_shim(uc, ctx, _format); \
}

// void NSLog(NSString * format, ...);
PRINTF_SHIM(NSLog, "v@");
// int snprintf_l(char * restrict str, size_t size, locale_t loc, const char * restrict format, ...);
PRINTF_SHIM(snprintf_l, "q*L**");
// [something somethingWithFormat:(NSString*)format, ...]
PRINTF_SHIM(NSSomethingWithFormat, "@@:@");
// +[NSException raise:(NSString*)name format:(NSString*)format, ...]
PRINTF_SHIM(NSException_raise_format, "@@:@@");

VPRINTF_SHIM(vsnprintf, snprintf, "i*Q*");
