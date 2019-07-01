//
//  c++abi.c
//  aah
//
//  Created by Jesús A. Álvarez on 11/05/2019.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import "aah.h"

extern int __cxa_atexit(void *f, void *p, void *d);

SHIMDEF(__cxa_atexit) {
    void *f, *p, *d;
    uc_reg_read(uc, UC_ARM64_REG_X0, &f);
    uc_reg_read(uc, UC_ARM64_REG_X1, &p);
    uc_reg_read(uc, UC_ARM64_REG_X2, &d);
    cif_cache_add(f, "v^v");
    __cxa_atexit(f, p, d);
    return SHIM_RETURN;
}
