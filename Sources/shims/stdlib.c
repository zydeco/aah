//
//  stdlib.c
//  aah
//
//  Created by Jesús A. Álvarez on 2019-10-07.
//  Copyright © 2019 namedfork. All rights reserved.
//

#include "aah.h"
#include "blocks.h"

WRAP_EMULATED_TO_NATIVE(sort) {
    void *fptr = *(void**)avalues[3];
    printf("adding qsort comparator at %p", fptr);
    cif_cache_add(fptr, "q^v^v", "(sort comparator)");
}

WRAP_EMULATED_TO_NATIVE(bsearch_b) {
    void *block = *(void**)avalues[4];
    cif_cache_block(block, "(bsearch_b comparator)");
}
