//
//  objc_msgSend.m
//  aah
//
//  Created by Jesús A. Álvarez on 01/05/2019.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import "aah.h"
#import <objc/runtime.h>
#import <objc/message.h>
#import <dlfcn.h>

static uint64_t shim_objc_msgSendCommon(uc_engine *uc, struct native_call_context *ctx, int is_super) {
    id receiver;
    Class cls;
    if (is_super) {
        struct objc_super *super = (struct objc_super*)ctx->arm64_call_context->x[0];
        receiver = super->receiver;
        cls = super->super_class;
        if (is_super == 2) {
            cls = class_getSuperclass(cls);
        }
        printf("objc_msgSendSuper%s with class %s\n", is_super == 2 ? "2" : "", class_getName(cls));
    } else {
        receiver = (id)ctx->arm64_call_context->x[0];
        cls = object_getClass(receiver);
    }
    if (receiver == nil) {
        uint64_t ret = 0;
        uc_reg_write(uc, UC_ARM64_REG_X0, &ret);
        printf("objc_msgSend* nil\n");
        return SHIM_RETURN;
    }
    SEL op = (SEL)ctx->arm64_call_context->x[1];
    IMP impl = class_getMethodImplementation(cls, op);
    BOOL meta = class_isMetaClass(cls);
    Dl_info info;
    printf("%c[%s %s]\n", (meta ? '+' : '-'), class_getName(cls), sel_getName(op));
    if (dladdr(impl, &info)) {
        if (should_emulate_image(info.dli_fbase)) {
            // calling emulated method
            printf("calling emulated method %s at %p\n", info.dli_sname, impl);
            if (is_super) {
                uc_reg_write(uc, UC_ARM64_REG_X0, &receiver);
            }
            return (uint64_t)impl;
        } else {
            // calling native method
            ffi_cif *cif_native = cif_cache_get_native(impl);
            ffi_cif_arm64 *cif_arm64 = cif_cache_get_arm64(impl);
            if (cif_native == NULL && cif_arm64 == NULL) {
                // check if there's a shim for this method
                // TODO: check if it's a wrapper
                char *methodName;
                asprintf(&methodName, "%c[%s %s]", (meta ? '+' : '-'), class_getName(cls), sel_getName(op));
                const char *methodSignature = lookup_method_signature(CIF_LIB_OBJC_SHIMS, methodName);
                if (methodSignature == NULL) {
                    methodSignature = method_getTypeEncoding(class_getInstanceMethod(cls, op));
                    if (methodSignature == NULL) {
                        // CIF is needed for forwarding the call
                        printf("could not find cif for %s\n", methodName);
                        // TODO: try to guess it?
                    } else {
                        printf("caching cif for %s with type encoding %s\n", methodName, methodSignature);
                    }
                } else {
                    printf("caching shim for %s\n", methodName);
                }
                free(methodName);
                cif_cache_add(impl, methodSignature);
                cif_native = cif_cache_get_native(impl);
                cif_arm64 = cif_cache_get_arm64(impl);
            }
            
            if (CIF_IS_CIF(cif_native) && cif_arm64) {
                // call
                ctx->cif_native = cif_native;
                ctx->cif_arm64 = cif_arm64;
                ctx->pc = (uint64_t)impl;
                ctx->before = ctx->after = NULL;
                if (is_super) {
                    ctx->arm64_call_context->x[0] = (uint64_t)receiver;
                }
                call_native_with_context(uc, ctx);
                return SHIM_RETURN;
            } else if (cif_native == CIF_MARKER_SHIM && cif_arm64 != NULL) {
                // shim
                shim_ptr shim = (shim_ptr)cif_arm64;
                return shim(uc, ctx);
            } else if (cif_native == CIF_MARKER_WRAPPER && cif_arm64 != NULL) {
                // wrapper
                struct call_wrapper *wrapper = (struct call_wrapper*)ctx->cif_arm64;
                ctx->cif_native = wrapper->cif_native;
                ctx->cif_arm64 = wrapper->cif_arm64;
                ctx->before = wrapper->emulated_to_native;
                ctx->after = wrapper->native_to_emulated;
                call_native_with_context(uc, ctx);
                return SHIM_RETURN;
            } else {
                abort();
            }
        }
    } else {
        fprintf(stderr, "calling %p, but found symbol at %p\n", impl, info.dli_saddr);
        abort();
    }
}

SHIMDEF(objc_msgSend) {
    return shim_objc_msgSendCommon(uc, ctx, 0);
}

SHIMDEF(objc_msgSendSuper) {
    return shim_objc_msgSendCommon(uc, ctx, 1);
}

SHIMDEF(objc_msgSendSuper2) {
    return shim_objc_msgSendCommon(uc, ctx, 2);
}
