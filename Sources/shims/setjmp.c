//
//  setjmp.c
//  aah
//
//  Created by Jesús A. Álvarez on 2019-07-10.
//  Copyright © 2019 namedfork. All rights reserved.
//

#include "aah.h"

// assume there are no jumps between native and emulated code
// TODO: check if there are jumps between native and emulated code

struct arm64_jmpbuf {
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t fp; // x29
    uint64_t lr; // x30
    uint64_t sp;
    uint64_t d8;
    uint64_t d9;
    uint64_t d10;
    uint64_t d11;
    uint64_t d12;
    uint64_t d13;
    uint64_t d14;
    uint64_t d15;
    uint64_t magic;
};

SHIMDEF(setjmp) {
    // save context
    struct arm64_jmpbuf *jmpbuf = (void*)ctx->arm64_call_context->x[0];
    uc_reg_read(uc, UC_ARM64_REG_X19, &jmpbuf->x19);
    uc_reg_read(uc, UC_ARM64_REG_X20, &jmpbuf->x20);
    uc_reg_read(uc, UC_ARM64_REG_X21, &jmpbuf->x21);
    uc_reg_read(uc, UC_ARM64_REG_X22, &jmpbuf->x22);
    uc_reg_read(uc, UC_ARM64_REG_X23, &jmpbuf->x23);
    uc_reg_read(uc, UC_ARM64_REG_X24, &jmpbuf->x24);
    uc_reg_read(uc, UC_ARM64_REG_X25, &jmpbuf->x25);
    uc_reg_read(uc, UC_ARM64_REG_X26, &jmpbuf->x26);
    uc_reg_read(uc, UC_ARM64_REG_X27, &jmpbuf->x27);
    uc_reg_read(uc, UC_ARM64_REG_X28, &jmpbuf->x28);
    uc_reg_read(uc, UC_ARM64_REG_FP, &jmpbuf->fp);
    uc_reg_read(uc, UC_ARM64_REG_LR, &jmpbuf->lr);
    uc_reg_read(uc, UC_ARM64_REG_SP, &jmpbuf->sp);
    uc_reg_read(uc, UC_ARM64_REG_D8, &jmpbuf->d8);
    uc_reg_read(uc, UC_ARM64_REG_D9, &jmpbuf->d9);
    uc_reg_read(uc, UC_ARM64_REG_D10, &jmpbuf->d10);
    uc_reg_read(uc, UC_ARM64_REG_D11, &jmpbuf->d11);
    uc_reg_read(uc, UC_ARM64_REG_D12, &jmpbuf->d12);
    uc_reg_read(uc, UC_ARM64_REG_D13, &jmpbuf->d13);
    uc_reg_read(uc, UC_ARM64_REG_D14, &jmpbuf->d14);
    uc_reg_read(uc, UC_ARM64_REG_D15, &jmpbuf->d15);
    
    // return 0
    ctx->arm64_call_context->x[0] = 0;
    return SHIM_RETURN;
}

SHIMDEF(longjmp) {
    // load context
    struct arm64_jmpbuf *jmpbuf = (void*)ctx->arm64_call_context->x[0];
    uc_reg_write(uc, UC_ARM64_REG_X19, &jmpbuf->x19);
    uc_reg_write(uc, UC_ARM64_REG_X20, &jmpbuf->x20);
    uc_reg_write(uc, UC_ARM64_REG_X21, &jmpbuf->x21);
    uc_reg_write(uc, UC_ARM64_REG_X22, &jmpbuf->x22);
    uc_reg_write(uc, UC_ARM64_REG_X23, &jmpbuf->x23);
    uc_reg_write(uc, UC_ARM64_REG_X24, &jmpbuf->x24);
    uc_reg_write(uc, UC_ARM64_REG_X25, &jmpbuf->x25);
    uc_reg_write(uc, UC_ARM64_REG_X26, &jmpbuf->x26);
    uc_reg_write(uc, UC_ARM64_REG_X27, &jmpbuf->x27);
    uc_reg_write(uc, UC_ARM64_REG_X28, &jmpbuf->x28);
    uc_reg_write(uc, UC_ARM64_REG_FP, &jmpbuf->fp);
    // lr doesn't need to be restored, jumping to it after shim
    uc_reg_write(uc, UC_ARM64_REG_SP, &jmpbuf->sp);
    uc_reg_write(uc, UC_ARM64_REG_D8, &jmpbuf->d8);
    uc_reg_write(uc, UC_ARM64_REG_D9, &jmpbuf->d9);
    uc_reg_write(uc, UC_ARM64_REG_D10, &jmpbuf->d10);
    uc_reg_write(uc, UC_ARM64_REG_D11, &jmpbuf->d11);
    uc_reg_write(uc, UC_ARM64_REG_D12, &jmpbuf->d12);
    uc_reg_write(uc, UC_ARM64_REG_D13, &jmpbuf->d13);
    uc_reg_write(uc, UC_ARM64_REG_D14, &jmpbuf->d14);
    uc_reg_write(uc, UC_ARM64_REG_D15, &jmpbuf->d15);
    
    // return val
    uint64_t val = ctx->arm64_call_context->x[1];
    ctx->arm64_call_context->x[0] = val;
    return jmpbuf->lr;
}
