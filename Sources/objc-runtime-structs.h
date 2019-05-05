//
//  objc-runtime-structs.h
//  aah
//
//  Created by Jesús A. Álvarez on 04/05/2019.
//  Copyright © 2019 namedfork. All rights reserved.
//

#ifndef objc_runtime_structs_h
#define objc_runtime_structs_h

struct method_t {
    const char *name;
    const char *types;
    void *imp;
};

struct method_list_t {
    uint32_t entsize_NEVER_USE;  // high bits used for fixup markers
    uint32_t count;
    struct method_t method[];
};

struct class_ro_t {
    uint32_t flags;
    uint32_t instanceStart;
    uint32_t instanceSize;
    uint32_t reserved;
    
    const uint8_t * ivarLayout;
    
    const char * name;
    const struct method_list_t * baseMethods;
    const void * baseProtocols;
    const void * ivars;
    
    const uint8_t * weakIvarLayout;
    const void *baseProperties;
};

struct class64 {
    struct class64 *isa;
    struct class64 *superclass;
    void *cache;
    void *vtable;
    struct class_ro_t *data;
};

struct category_t {
    const char *name;
    const struct classref *cls;
    const struct method_list_t *instanceMethods;
    const struct method_list_t *classMethods;
    const void *protocols;
    const void *instanceProperties;
};

#endif /* objc_runtime_structs_h */
