//
//  libdispatch.c
//  aah
//
//  Created by Jesús A. Álvarez on 03/07/2019.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import "aah.h"

struct Block_layout {
    void *isa;
    volatile int32_t flags; // contains ref count
    int32_t reserved;
    void (*invoke)(void *, ...);
    struct Block_descriptor_1 *descriptor;
    // imported variables
};

// void function(void * arg, dispatch_block_t block);
// dispatch_once, dispatch_async
WRAP_EMULATED_TO_NATIVE(dispatch_block_1) {
    struct Block_layout *block = *(void**)avalues[1];
    printf("caching cif for block %p\n", block->invoke);
    cif_cache_add(block->invoke, "v", "(block)");
}

WRAP_EMULATED_TO_NATIVE(dispatch_once_f) {
    void *fptr = *(void**)avalues[1];
    printf("caching cif for block %p\n", fptr);
    cif_cache_add(fptr, "v", "(block)");
}

WRAP_EMULATED_TO_NATIVE(dispatch_async_f) {
    void *fptr = *(void**)avalues[2];
    printf("caching cif for block %p\n", fptr);
    cif_cache_add(fptr, "v", "(block)");
}
