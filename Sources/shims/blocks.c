//
//  blocks.c
//  aah
//
//  Created by Jesús A. Álvarez on 2019-07-07.
//  Copyright © 2019 namedfork. All rights reserved.
//

#include "aah.h"
#include "blocks.h"

void cif_cache_block(const void *arg) {
    struct Block_layout *block = (struct Block_layout*)arg;
    if (block) {
        cif_cache_add(block->invoke, _Block_has_signature(block) ? _Block_signature(block) : "v^?", "(block)");
        if (block->descriptor) {
            if (block->descriptor->copy) {
                cif_cache_add(block->descriptor->copy, "v^?^?", "(block copy helper)");
            }
            if (block->descriptor->dispose) {
                cif_cache_add(block->descriptor->dispose, "v^?", "(block dispose helper)");
            }
        }
    }
}

void *aah_Block_copy(const void *arg) {
    cif_cache_block(arg);
    return _Block_copy(arg);
}

enum {
    // see function implementation for a more complete description of these fields and combinations
    BLOCK_FIELD_IS_OBJECT   =  3,  // id, NSObject, __attribute__((NSObject)), block, ...
    BLOCK_FIELD_IS_BLOCK    =  7,  // a block variable
    BLOCK_FIELD_IS_BYREF    =  8,  // the on stack structure holding the __block variable
    BLOCK_FIELD_IS_WEAK     = 16,  // declared __weak, only used in byref copy helpers
    BLOCK_BYREF_CALLER      = 128, // called from __block (byref) copy/dispose support routines.
};

void aah_Block_object_assign(void * destAddr, const void * object, const int flags) {
    if ((flags & BLOCK_FIELD_IS_BLOCK) == BLOCK_FIELD_IS_BLOCK) {
        cif_cache_block(object);
    }
    _Block_object_assign(destAddr, object, flags);
}

ffi_type aah_type_block_pointer = {
    .size = 8,
    .alignment = 8,
    .type = FFI_TYPE_POINTER,
    .elements = NULL
};
