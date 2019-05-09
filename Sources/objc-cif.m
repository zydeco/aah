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
        if (only_emulated) {
            Dl_info info;
            if (dladdr(imp, &info) && !should_emulate_image(info.dli_fbase)) {
                continue;
            }
        }
        const char *typeEncoding = method_getTypeEncoding(m);
        if (strlen(typeEncoding) > 0 && strchr(typeEncoding, '<') == 0 && strchr(typeEncoding, ',') == 0) {
            cif_cache_add(imp, typeEncoding);
        }
    }
}

hidden void cif_cache_add_class(const char *name) {
    cif_cache_add_methods(objc_getClass(name), false);
    cif_cache_add_methods(objc_getMetaClass(name), false);
}

hidden void load_objc_entrypoints() {
    unsigned int nclasses;
    Class *classes = objc_copyClassList(&nclasses);
    for (int i=0; i < nclasses; i++) {
        cif_cache_add_methods(classes[i], true);
        cif_cache_add_methods(objc_getMetaClass(class_getName(classes[i])), true);
    }
    free(classes);
}
