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

// arm64 has alternate string layout!

union cpp_string {
    union cpp_alternate_string {
        struct cpp_alternate_long_string {
            char *data;
            uint64_t size;
            uint64_t cap;
        } __long;
        struct cpp_alternate_short_string {
            char data[23];
            uint8_t size;
        } __short;
    } alternate;
    union cpp_native_string {
        struct cpp_native_long_string {
            uint64_t cap;
            uint64_t size;
            char *data;
        } __long;
        struct cpp_native_short_string {
            uint8_t size;
            char data[23];
        } __short;
    } native;
};

void swap_cpp_string(union cpp_string *str) {
    // from alternate to native
    uint8_t short_size = str->alternate.__short.size;
    if (short_size & 0x80) {
        uint64_t tmp = (uint64_t)str->alternate.__long.data;
        str->alternate.__long.data = (char*)str->alternate.__long.cap;
        str->alternate.__long.cap = tmp;
    } else {
        memmove(str->native.__short.data, str->alternate.__short.data, 23);
        str->native.__short.size = short_size << 1;
    }
}

void unswap_cpp_string(union cpp_string *str) {
    // from native to alternate
    uint8_t short_size = str->native.__short.size;
    if (short_size & 0x01) {
        uint64_t tmp = (uint64_t)str->alternate.__long.data;
        str->alternate.__long.data = (char*)str->alternate.__long.cap;
        str->alternate.__long.cap = tmp;
    } else {
        memmove(str->alternate.__short.data, str->native.__short.data, 23);
        str->alternate.__short.size = short_size >> 1;
    }
}

WRAP_BEFORE(cpp_string_0) { swap_cpp_string(*(union cpp_string**)(avalues[0])); }
WRAP_AFTER(cpp_string_0) { unswap_cpp_string(*(union cpp_string**)(avalues[0])); }

WRAP_BEFORE(cpp_string_1) { swap_cpp_string(*(union cpp_string**)(avalues[1])); }
WRAP_AFTER(cpp_string_1) { unswap_cpp_string(*(union cpp_string**)(avalues[1])); }

WRAP_BEFORE(cpp_string_01) {
    swap_cpp_string(*(union cpp_string**)(avalues[0]));
    swap_cpp_string(*(union cpp_string**)(avalues[1]));
}
WRAP_AFTER(cpp_string_01) {
    unswap_cpp_string(*(union cpp_string**)(avalues[0]));
    unswap_cpp_string(*(union cpp_string**)(avalues[1]));
}

// libffi can't call this?
// should be returned in memory because it's an object
// but libffi thinks it's returned in a register because it fits
// should add UNIX64_FLAG_RET_IN_MEM flag to native cif
extern void _ZNKSt3__18ios_base6getlocEv(void *p1, void *p2);
SHIMDEF(ios_base_getloc) {
    void *p1, *p2;
    uc_reg_read(uc, UC_ARM64_REG_X8, &p1);
    uc_reg_read(uc, UC_ARM64_REG_X0, &p2);
    printf("(%p)ios_base::getloc returning at %p\n", p2, p1);
    _ZNKSt3__18ios_base6getlocEv(p1, p2);
    return SHIM_RETURN;
}
