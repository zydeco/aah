//
//  stdlib.c
//  aah
//
//  Created by Jesús A. Álvarez on 2019-10-07.
//  Copyright © 2019 namedfork. All rights reserved.
//

#include "aah.h"

WRAP_EMULATED_TO_NATIVE(sort) {
    void *fptr = *(void**)avalues[3];
    printf("adding qsort comparator at %p", fptr);
    cif_cache_add(fptr, "q^v^v", "(sort comparator)");
}
