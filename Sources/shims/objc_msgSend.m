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

static void shim_objc_msgSendCommon(uc_engine *uc, struct native_call_context *ctx, int is_super) {
    id receiver;
    Class cls;
    if (is_super) {
        struct objc_super *super = (struct objc_super*)ctx->arm64_call_context->x[0];
        receiver = super->receiver;
        cls = super->super_class;
        if (is_super == 2) {
            cls = class_getSuperclass(cls);
        }
        printf("objc_msgSendSuper%d with class %s\n", is_super, class_getName(cls));
    } else {
        receiver = (id)ctx->arm64_call_context->x[0];
        cls = object_getClass(receiver);
    }
    if (receiver == nil) {
        uint64_t ret = 0;
        uc_reg_write(uc, UC_ARM64_REG_X0, &ret);
        return;
    }
    SEL msg = (SEL)ctx->arm64_call_context->x[1];
    IMP impl = class_getMethodImplementation(cls, msg);
    Dl_info info;
    printf("[%s %s]\n", class_getName(cls), sel_getName(msg));
    if (dladdr(impl, &info) && info.dli_saddr == impl) {
        if (should_emulate_image(info.dli_fbase)) {
            // calling emulated method
            printf("calling emulated method %s\n", info.dli_sname);
            run_emulator(get_emulator_ctx(), (uint64_t)impl);
        } else {
            // TODO: check if method is variadic (should have shim then)
            
            // calling native method
            const char * typeEncoding = method_getTypeEncoding(class_getInstanceMethod(cls, msg));
            printf("calling native method %s with type encoding %s\n", info.dli_sname, typeEncoding);
            // construct call
            ffi_cif cif_native;
            ffi_cif_arm64 cif_arm64;
            prep_cifs(&cif_native, &cif_arm64, typeEncoding, -1);
            ctx->cif_native = &cif_native;
            ctx->cif_arm64 = &cif_arm64;
            ctx->pc = (uint64_t)impl;
            call_native_with_context(uc, ctx);
            free(cif_native.arg_types); // cif_arm64.arg_types is the same
        }
    } else {
        fprintf(stderr, "calling %p, but foudn symbol at %p\n", impl, info.dli_saddr);
        abort();
    }
}

SHIMDEF(objc_msgSend) {
    shim_objc_msgSendCommon(uc, ctx, 0);
}

SHIMDEF(objc_msgSendSuper) {
    shim_objc_msgSendCommon(uc, ctx, 1);
}

SHIMDEF(objc_msgSendSuper2) {
    shim_objc_msgSendCommon(uc, ctx, 2);
}
