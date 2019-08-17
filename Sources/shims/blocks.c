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
        cif_cache_add(block->invoke, _Block_signature(block) ?: "v?", "(block)");
        if (block->descriptor) {
            if (block->descriptor->copy) {
                cif_cache_add(block->descriptor->copy, "v??", "(block copy helper)");
            }
            if (block->descriptor->dispose) {
                cif_cache_add(block->descriptor->dispose, "v?", "(block dispose helper)");
            }
        }
    }
}

void *my_Block_copy(const void *arg) {
    cif_cache_block(arg);
    return _Block_copy(arg);
}

ffi_type aah_type_block_pointer = {
    .size = 8,
    .alignment = 8,
    .type = FFI_TYPE_POINTER,
    .elements = NULL
};
