//
//  objc-cif.m
//  aah
//
//  Created by Jesús A. Álvarez on 06/05/2019.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import "aah.h"
#import <objc/runtime.h>

static void cif_cache_add_methods(Class cls) {
    unsigned int methodCount;
    Method *methods = class_copyMethodList(cls, &methodCount);
    for (unsigned int i = 0; i < methodCount; i++) {
        Method m = methods[i];
        IMP imp = method_getImplementation(m);
        const char *typeEncoding = method_getTypeEncoding(m);
        cif_cache_add(imp, typeEncoding);
    }
}

hidden void cif_cache_add_class(const char *name) {
    cif_cache_add_methods(objc_getClass(name));
    cif_cache_add_methods(objc_getMetaClass(name));
}
