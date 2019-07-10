//
//  libdispatch.c
//  aah
//
//  Created by Jesús A. Álvarez on 03/07/2019.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import "aah.h"
#import "blocks.h"

// void function(void * arg, dispatch_block_t block);
// dispatch_once, dispatch_async
WRAP_EMULATED_TO_NATIVE(dispatch_block_1) {
    struct Block_layout *block = *(void**)avalues[1];
    const char *signature = _Block_signature(block) ?: "v?";
    printf("caching cif for block %p (%s)\n", block->invoke, signature);
    cif_cache_add(block->invoke, signature, "(block)");
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
