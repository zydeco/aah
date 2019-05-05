#include "aah.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <CoreFoundation/CoreFoundation.h>
#include <os/lock.h>

static CFMutableDictionaryRef cif_cache_native = NULL;
static CFMutableDictionaryRef cif_cache_arm64 = NULL;
static os_unfair_lock cif_cache_lock = OS_UNFAIR_LOCK_INIT;
static CFDictionaryRef cif_sig_table = NULL;

const char *CIF_LIB_OBJC_SHIMS = "objc shims";

//#define P(...) printf(__VA_ARGS__)
#define P(...)

hidden void init_cif() {
    // initialize cif cache
    cif_cache_native = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
    cif_cache_arm64 = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, NULL, NULL);
    cif_cache_add(dlsym(RTLD_DEFAULT, "main"), "ii???");
    
    // load method signature table
    Dl_info info;
    if (dladdr(&init_cif, &info) == 0) {
        fprintf(stderr, "couldn't find myself\n");
        abort();
    }
    uint32_t image_index = 0;
    uint32_t num_images = _dyld_image_count();
    for(uint32_t i = 0; i < num_images; i++) {
        if (_dyld_get_image_header(i) == info.dli_fbase) {
            printf("Found myself at %d\n", i);
            image_index = i;
            break;
        }
    }
    intptr_t vmaddr_slide = _dyld_get_image_vmaddr_slide(image_index);
    const struct section_64 *sig_table = getsectbynamefromheader_64(info.dli_fbase, SEG_DATA, "__aah_meth_sigs");
    void *sig_bytes = (void *)((uintptr_t)vmaddr_slide + sig_table->addr);
    size_t sig_length = sig_table->size;
    CFDataRef sig_data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, sig_bytes, sig_length, kCFAllocatorNull);
    cif_sig_table = CFPropertyListCreateWithData(kCFAllocatorDefault, sig_data, kCFPropertyListImmutable, NULL, NULL);
    CFRelease(sig_data);
}

static ffi_type *next_type(char const ** method_signature, const char *prefix) {
    ffi_type *type = NULL;
    const char *ms = *method_signature;
    P("next_type %s\n", ms);
next_type_1:
    switch(*ms++) {
        case 'c': 
            P("%sffi_type_sint8\n", prefix);
            type = &ffi_type_sint8;
            break;
        case 's':
            P("%sffi_type_sint16\n", prefix);
            type = &ffi_type_sint16;
            break;
        case 'i':
        case 'l': 
            P("%sffi_type_sint32\n", prefix);
            type = &ffi_type_sint32;
            break;
        case 'q': 
            P("%sffi_type_sint64\n", prefix);
            type = &ffi_type_sint64;
            break;
        case 'C':
            P("%sffi_type_uint8\n", prefix);
            type = &ffi_type_uint8;
            break;
        case 'S': 
            P("%sffi_type_uint16\n", prefix);
            type = &ffi_type_uint16;
            break;
        case 'I':
        case 'L':
            P("%sffi_type_uint32\n", prefix);
            type = &ffi_type_uint32;
            break;
        case 'Q': 
            P("%sffi_type_uint64\n", prefix);
            type = &ffi_type_uint64;
            break;
        case 'f': 
            P("%sffi_type_float\n", prefix);
            type = &ffi_type_float;
            break;
        case 'd': 
            P("%sffi_type_double\n", prefix);
            type = &ffi_type_double;
            break;
        case 'D':
            P("%sffi_type_longdouble\n", prefix);
            type = &ffi_type_longdouble;
            break;
        case 'B': 
            P("%sffi_type_uint8\n", prefix);
            type = &ffi_type_uint8;
            break;
        case 'v': 
            P("%sffi_type_void\n", prefix);
            type = &ffi_type_void;
            break;
        case '^':
            next_type(&ms, "skipping pointer: ");
            // fall through
        case ':':
        case '#':
        case '*':
        case '@':
        case '?':
            P("%sffi_type_pointer\n", prefix);
            type = &ffi_type_pointer;
            break;
        case '[': { // array
            // doesn't appear in method signatures, but could be in structures
            unsigned long nitems = strtoul(ms, (char**)&ms, 10);
            P("%sarray of %d\n", prefix, (int)nitems);
            ffi_type *element_type = next_type(&ms, "element type: ");
            type->size = type->alignment = 0;
            type->type = FFI_TYPE_STRUCT;
            type->elements = calloc(nitems + 1, sizeof(void*));
            for(unsigned long i = 0; i < nitems; i++) {
                type->elements[i] = element_type;
            }
            type->elements[nitems] = NULL;
            if (*ms++ != ']') fprintf(stderr, "missing array end\n");
            } break;
        case '{': { // struct
            P("struct\n");
            ms = strchr(ms, '=') + 1;
            type->size = type->alignment = 0;
            type->type = FFI_TYPE_STRUCT;
            int maxelems = 15;
            type->elements = calloc(maxelems + 1, sizeof(void*));
            int elem = 0;
            while(*ms != '}') {
                if (elem == maxelems) {
                    maxelems += 8;
                    type->elements = realloc(type->elements, (maxelems + 1) * sizeof(void*));
                }
                type->elements[elem++] = next_type(&ms, "struct member: ");
            }
            type->elements[elem] = NULL;
        } break;
        case '(': // TODO: union
            fprintf(stderr, "unions not supported in method signature: %s", ms-1);
            abort();
        case 'b': // TODO: bitfield
            fprintf(stderr, "bitfields not supported in method signature: %s", ms-1);
            abort();
        case 'r': // const
        case 'n': // in
        case 'N': // inout
        case 'o': // out
        case 'O': // bycopy
        case 'R': // byref
        case 'V': // oneway
            // skip qualifiers
            goto next_type_1;
        default:
            fprintf(stderr, "unexpected character in method signature: %s\n", ms-1);
            abort();
    }
    
    // skip offset
    while(*ms >= '0' && *ms <= '9') {
        ms++;
    }
    *method_signature = ms;
    return type;
}

hidden int prep_cifs(ffi_cif *cif, ffi_cif_arm64 *cif_arm64, const char *method_signature, int fixed_args) {
    const char *ms = method_signature;
    if (ms == NULL) return 0;
    
    P("parsing method signature %s\n", method_signature);
    ffi_type *rtype = next_type(&ms, "return type: ");
    unsigned int nargs = 0;
    unsigned int maxargs = 8;
    ffi_type **argtypes = calloc(maxargs, sizeof(void*));
    while (*ms) {
        if (nargs == maxargs) {
            maxargs += 8;
            argtypes = realloc(argtypes, maxargs);
        }
        argtypes[nargs++] = next_type(&ms, "arg: ");
    }
    
    if (fixed_args == -1) {
        ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, rtype, argtypes);
        ffi_prep_cif_arm64(cif_arm64, 0, nargs, nargs, rtype, argtypes);
    } else {
        ffi_prep_cif_var(cif, FFI_DEFAULT_ABI, fixed_args, nargs, rtype, argtypes);
        ffi_prep_cif_arm64(cif_arm64, 1, fixed_args, nargs, rtype, argtypes);
    }

    P("cif done, had %u args\n", nargs);
    return 1;
}

hidden ffi_cif * cif_cache_get_native(void *address) {
    os_unfair_lock_lock(&cif_cache_lock);
    ffi_cif *cif = (ffi_cif *)CFDictionaryGetValue(cif_cache_native, address);
    os_unfair_lock_unlock(&cif_cache_lock);
    return cif;
}

hidden ffi_cif_arm64 * cif_cache_get_arm64(void *address) {
    os_unfair_lock_lock(&cif_cache_lock);
    ffi_cif_arm64 *cif = (ffi_cif_arm64 *)CFDictionaryGetValue(cif_cache_arm64, address);
    os_unfair_lock_unlock(&cif_cache_lock);
    return cif;
}

hidden void cif_cache_add(void *address, const char *method_signature) {
    ffi_cif *cif = malloc(sizeof(ffi_cif));
    ffi_cif_arm64 *cif_arm64 = malloc(sizeof(ffi_cif_arm64));
    if (method_signature[0] == '$') {
        // shim
        os_unfair_lock_lock(&cif_cache_lock);
        CFDictionarySetValue(cif_cache_native, address, 0); // shim marker
        char shim_name[128];
        snprintf(shim_name, 128, "aah_shim_%s", method_signature+1);
        void *shim = dlsym(RTLD_SELF, shim_name);
        if (shim == NULL) {
            printf("shim not found: %s, might crash later\n", shim_name);
        }
        CFDictionarySetValue(cif_cache_arm64, address, shim); // shim pointer
        os_unfair_lock_unlock(&cif_cache_lock);
    } else if (prep_cifs(cif, cif_arm64, method_signature, -1)) {
        os_unfair_lock_lock(&cif_cache_lock);
        if (!CFDictionaryContainsKey(cif_cache_native, address)) {
            CFDictionarySetValue(cif_cache_native, address, cif);
            CFDictionarySetValue(cif_cache_arm64, address, cif_arm64);
        }
        os_unfair_lock_unlock(&cif_cache_lock);
    }
}

hidden const char * lookup_method_signature(const char *lib_name, const char *sym_name) {
    // read local table of method signatures
    CFStringRef lib_name_cf = CFStringCreateWithCStringNoCopy(kCFAllocatorDefault, lib_name, kCFStringEncodingUTF8, kCFAllocatorNull);
    CFDictionaryRef lib_table = CFDictionaryGetValue(cif_sig_table, lib_name_cf);
    CFRelease(lib_name_cf);
    if (lib_table == NULL) {
        printf("Library not found in table: %s\n", lib_name);
        return NULL;
    }
    CFStringRef sym_name_cf = CFStringCreateWithCStringNoCopy(kCFAllocatorDefault, sym_name, kCFStringEncodingUTF8, kCFAllocatorNull);
    CFStringRef signature = CFDictionaryGetValue(lib_table, sym_name_cf);
    CFRelease(sym_name_cf);
    if (signature == NULL) {
        if (lib_name != CIF_LIB_OBJC_SHIMS) {
            printf("Symbol %s not found in table for library %s\n", sym_name, lib_name);
        }
        return NULL;
    }
    const char *ms = CFStringGetCStringPtr(signature, kCFStringEncodingUTF8);
    if (ms == NULL) {
        char buf[512];
        printf("leaking string\n");
        CFStringGetCString(signature, buf, sizeof buf, kCFStringEncodingUTF8);
        ms = strdup(buf); // will leak
    }
    return ms;
}

static void call_native_function(ffi_cif_arm64 *cif_arm64, void *rvalue, void **avalues, void * user_data) {
    struct native_call_context *ctx = (struct native_call_context *)user_data;
    ffi_call(ctx->cif_native, (void*)ctx->pc, rvalue, avalues);
}

hidden void call_native_with_context(uc_engine *uc, struct native_call_context *ctx) {
    // call
    void *ret = ctx->cif_arm64->rtype->type == FFI_TYPE_VOID ? NULL : alloca(ctx->cif_native->rtype->size);
    ffi_closure_SYSV_inner_arm64(ctx->cif_arm64, call_native_function, ctx, ctx->arm64_call_context, (void*)ctx->sp, ret, ret);
    
    // pass return value to emulator
    int rflags = arm64_rflags_for_type(ctx->cif_arm64->rtype);
    uint64_t rr0 = 0;
    if (rflags & AARCH64_RET_IN_MEM) {
        // copy to x8
        uint64_t x8;
        uc_reg_read(uc, UC_ARM64_REG_X8, &x8);
        memcpy((void*)x8, ret, ctx->cif_native->rtype->size);
    } else switch(rflags & AARCH64_RET_MASK) {
        case AARCH64_RET_VOID:
            break;
        case AARCH64_RET_INT128:
            uc_reg_write(uc, UC_ARM64_REG_X1, ret+8);
        case AARCH64_RET_INT64:
            uc_reg_write(uc, UC_ARM64_REG_X0, ret);
            break;
        case AARCH64_RET_UINT8:
            rr0 = *(uint8_t*)ret;
            uc_reg_write(uc, UC_ARM64_REG_X0, &rr0);
            break;
        case AARCH64_RET_SINT8:
            rr0 = *(int8_t*)ret;
            uc_reg_write(uc, UC_ARM64_REG_X0, &rr0);
            break;
        case AARCH64_RET_UINT16:
            rr0 = *(uint16_t*)ret;
            uc_reg_write(uc, UC_ARM64_REG_X0, &rr0);
            break;
        case AARCH64_RET_SINT16:
            rr0 = *(int16_t*)ret;
            uc_reg_write(uc, UC_ARM64_REG_X0, &rr0);
            break;
        case AARCH64_RET_UINT32:
            rr0 = *(uint32_t*)ret;
            uc_reg_write(uc, UC_ARM64_REG_X0, &rr0);
            memcpy(ret, &rr0, 4);
            break;
        case AARCH64_RET_SINT32:
            rr0 = *(int32_t*)ret;
            uc_reg_write(uc, UC_ARM64_REG_X0, &rr0);
            memcpy(ret, &rr0, 4);
            break;
    
        case AARCH64_RET_S4:
            uc_reg_write(uc, UC_ARM64_REG_S3, ret+12);
        case AARCH64_RET_S3:
            uc_reg_write(uc, UC_ARM64_REG_S2, ret+8);
        case AARCH64_RET_S2:
            uc_reg_write(uc, UC_ARM64_REG_S1, ret+4);
        case AARCH64_RET_S1:
            uc_reg_write(uc, UC_ARM64_REG_S0, ret);
            break;
    
        case AARCH64_RET_D4:
            uc_reg_write(uc, UC_ARM64_REG_D3, ret+24);
        case AARCH64_RET_D3:
            uc_reg_write(uc, UC_ARM64_REG_D2, ret+16);
        case AARCH64_RET_D2:
            uc_reg_write(uc, UC_ARM64_REG_D1, ret+8);
        case AARCH64_RET_D1:
            uc_reg_write(uc, UC_ARM64_REG_D0, ret);
            break;
    
        case AARCH64_RET_Q4:
            uc_reg_write(uc, UC_ARM64_REG_Q3, ret+48);
        case AARCH64_RET_Q3:
            uc_reg_write(uc, UC_ARM64_REG_Q2, ret+32);
        case AARCH64_RET_Q2:
            uc_reg_write(uc, UC_ARM64_REG_Q1, ret+16);
        case AARCH64_RET_Q1:
            uc_reg_write(uc, UC_ARM64_REG_Q0, ret);
            break;
    
        default:
            printf("don't know how to return\n");
            abort();
    }
}

hidden uint64_t call_native(uc_engine *uc, uint64_t pc) {
    // call context
    struct arm64_call_context call_context;
    uint64_t sp;
    uc_reg_read(uc, UC_ARM64_REG_SP, &sp);
    uc_reg_read(uc, UC_ARM64_REG_X0, &call_context.x[0]);
    uc_reg_read(uc, UC_ARM64_REG_X1, &call_context.x[1]);
    uc_reg_read(uc, UC_ARM64_REG_X2, &call_context.x[2]);
    uc_reg_read(uc, UC_ARM64_REG_X3, &call_context.x[3]);
    uc_reg_read(uc, UC_ARM64_REG_X4, &call_context.x[4]);
    uc_reg_read(uc, UC_ARM64_REG_X5, &call_context.x[5]);
    uc_reg_read(uc, UC_ARM64_REG_X6, &call_context.x[6]);
    uc_reg_read(uc, UC_ARM64_REG_X7, &call_context.x[7]);
    uc_reg_read(uc, UC_ARM64_REG_X8, &call_context.x[8]);
    uc_reg_read(uc, UC_ARM64_REG_V0, &call_context.v[0]);
    uc_reg_read(uc, UC_ARM64_REG_V1, &call_context.v[1]);
    uc_reg_read(uc, UC_ARM64_REG_V2, &call_context.v[2]);
    uc_reg_read(uc, UC_ARM64_REG_V3, &call_context.v[3]);
    uc_reg_read(uc, UC_ARM64_REG_V4, &call_context.v[4]);
    uc_reg_read(uc, UC_ARM64_REG_V5, &call_context.v[5]);
    uc_reg_read(uc, UC_ARM64_REG_V6, &call_context.v[6]);
    uc_reg_read(uc, UC_ARM64_REG_V7, &call_context.v[7]);
    
    // find cif
    struct native_call_context ctx;
    os_unfair_lock_lock(&cif_cache_lock);
    ctx.cif_native = (ffi_cif *)CFDictionaryGetValue(cif_cache_native, (void*)pc);
    ctx.cif_arm64 = (ffi_cif_arm64 *)CFDictionaryGetValue(cif_cache_arm64, (void*)pc);
    ctx.pc = pc;
    ctx.sp = sp;
    ctx.arm64_call_context = &call_context;
    os_unfair_lock_unlock(&cif_cache_lock);
    if (ctx.cif_native && ctx.cif_arm64) {
        // call with cif
        call_native_with_context(uc, &ctx);
        return SHIM_RETURN;
    } else if (ctx.cif_native == NULL && ctx.cif_arm64 != NULL) {
        // call shim
        shim_ptr shim = (shim_ptr)ctx.cif_arm64;
        ctx.cif_arm64 = NULL;
        printf("calling shim at %p\n", shim);
        return shim(uc, &ctx);
    } else {
        printf("missing cif\n");
        abort();
    }
}
