//
//  objc-cif.m
//  aah
//
//  Created by Jesús A. Álvarez on 06/05/2019.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import "aah.h"
#import <objc/runtime.h>

static void cif_cache_add_methods(Class cls, bool only_emulated) {
    unsigned int methodCount;
    Method *methods = class_copyMethodList(cls, &methodCount);
    for (unsigned int i = 0; i < methodCount; i++) {
        Method m = methods[i];
        IMP imp = method_getImplementation(m);
        if (only_emulated && !should_emulate_at((uint64_t)imp)) {
            continue;
        }
        const char *typeEncoding = method_getTypeEncoding(m);
        if (strlen(typeEncoding) > 0 && strchr(typeEncoding, '<') == 0 && strchr(typeEncoding, ',') == 0) {
            char *name = NULL;
            asprintf(&name, "%c[%s %s]", class_isMetaClass(cls) ? '+' : '-', class_getName(cls), sel_getName(method_getName(m)));
            cif_cache_add(imp, typeEncoding, name);
        }
    }
}

hidden void cif_cache_add_class(const char *name) {
    cif_cache_add_methods(objc_getClass(name), false);
    cif_cache_add_methods(objc_getMetaClass(name), false);
}

struct method {
    const char *name;
    const char *types;
    void *implementation;
};

struct method_list {
    uint32_t entrySize;
    uint32_t count;
    struct method methods[];
};

struct class_ro {
    uint32_t flags;
    uint32_t instanceStart;
    uint32_t instanceSize;
    uint32_t reserved;
    const uint8_t * ivarLayout;
    const char * name;
    struct method_list * baseMethodList;
    void * baseProtocols; // protocol_list_t
    const void * ivars; // ivar_list_t
    const uint8_t * weakIvarLayout;
    void * baseProperties; // property_list_t
};

struct class_flags {
    uint32_t flags;
    uint32_t reserved[3];
};

struct class_rw {
    uint32_t flags;
    uint32_t version;
    struct class_ro *ro;
    struct method_list *methods;
    void *properties; // property_array_t
    void *protocols; // protocol_array_t
    void *firstSubclass; // Class
    void *nextSiblingClass; // Class
    char *demangledName;
};

struct classref {
    uint64_t isa;
    uint64_t superclass;
    uint64_t cache;
    uint64_t vtable;
    union {
        struct class_ro *ro;
        struct class_rw *rw;
        struct class_flags *flags;
    } data;
};

struct cat_info {
    const char *name;
    void *cls;
    struct method_list *instanceMethods;
    struct method_list *classMethods;
    void *protocols;
    void *instanceProperties;
};

#define RO_META 1
#define RW_COPIED_RO (1 << 27)
#define RW_REALIZING (1 << 19)

hidden void load_objc_methods(struct method_list *methods, bool meta, const char *name) {
    if (methods == NULL) {
        return;
    }
    for(uint32_t i = 0; i < methods->count; i++) {
        struct method * method = &methods->methods[i];
        char *method_name = NULL;
        asprintf(&method_name, "%c[%s %s]", meta ? '+' : '-', name, method->name);
        //printf("%s (%s) -> %p\n", method_name, method->types, method->implementation);
        const char *shimMethodSignature = lookup_method_signature(CIF_LIB_OBJC_SHIMS, method_name);
        if (shimMethodSignature) {
            cif_cache_add(method->implementation, shimMethodSignature, method_name);
        } else {
            cif_cache_add(method->implementation, method->types, method_name);
        }
    }
}

static inline struct class_ro* get_class_ro(struct classref *class) {
    if (class->data.flags->flags & (RW_COPIED_RO | RW_REALIZING)) {
        return class->data.rw->ro;
    } else {
        return class->data.ro;
    }
}

hidden void load_objc_classlist(const struct section_64 *classlist, intptr_t vmaddr_slide) {
    if (classlist) {
        uint64_t numClasses = classlist->size / 8;
        printf("loading %d classes\n", (int)numClasses);
        struct classref **classes = (struct classref**)(classlist->addr + vmaddr_slide);
        for(uint64_t i = 0; i < numClasses; i++) {
            struct classref *class = classes[i];
            struct class_ro *data = get_class_ro(class);
            bool is_metaclass = data->flags & RO_META;
            
            printf("loading class %p(%p): %s\n", class, data, data->name);
            printf("flags: %08x\n", data->flags);
            load_objc_methods(data->baseMethodList, is_metaclass, data->name);
            // superclass methods
            struct classref *isa = (struct classref*)class->isa;
            if (isa && isa != class) {
                struct class_ro *isa_ro = get_class_ro(isa);
                printf("super class %p(%p): %s\n", isa, isa_ro, isa_ro->name);
                load_objc_methods(isa_ro->baseMethodList, isa_ro->flags & RO_META, isa_ro->name);
            }
        }
    }
}

hidden void load_objc_catlist(const struct section_64 *catlist, intptr_t vmaddr_slide) {
    if (catlist) { // meow
        uint64_t numCats = catlist->size / 8;
        struct cat_info **cats = (struct cat_info**)(catlist->addr + vmaddr_slide);
        for(uint64_t i = 0; i < numCats; i++) {
            struct cat_info *cat = cats[i];
            load_objc_methods(cat->classMethods, true, cat->name);
            load_objc_methods(cat->instanceMethods, false, cat->name);
        }
    }
}

hidden const struct section_64 *getdatasectfromheader_64(const struct mach_header_64 *mh, const char *sectname) {
    return getsectbynamefromheader_64(mh, "__DATA_CONST", sectname) ?: getsectbynamefromheader_64(mh, "__DATA", sectname);
}

hidden void load_objc_entrypoints(const struct mach_header_64 *mh, intptr_t vmaddr_slide) {
    // load classes
    load_objc_classlist(getdatasectfromheader_64(mh, "__objc_classlist"), vmaddr_slide);
    load_objc_classlist(getdatasectfromheader_64(mh, "__objc_nlclslist"), vmaddr_slide);

    // load categories
    load_objc_catlist(getdatasectfromheader_64(mh, "__objc_catlist"), vmaddr_slide);
    load_objc_catlist(getdatasectfromheader_64(mh, "__objc_nlcatlist"), vmaddr_slide);
}
