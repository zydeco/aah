//
//  AAHCodeBrowser.m
//  TestApp
//
//  Created by Jesús A. Álvarez on 2019-10-06.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import "AAHCodeBrowser.h"
#import "AAHDisassembler.h"
#import <dlfcn.h>
#import <objc/runtime.h>
#import <objc/message.h>

@implementation AAHCodeBrowser

- (void*)findMethodWithName:(NSString*)methodName {
    void *addr = dlsym(RTLD_DEFAULT, methodName.UTF8String);
    if (addr == NULL && ([methodName hasPrefix:@"-["] || [methodName hasPrefix:@"+["]) && [methodName hasSuffix:@"]"]) {
        // Objective-C method
        NSArray<NSString*> *components = [[methodName substringWithRange:NSMakeRange(2, methodName.length-3)] componentsSeparatedByString:@" "];
        if (components.count != 2) return NULL;
        Class class = NSClassFromString(components[0]);
        if (class == NULL) return NULL;
        SEL name = NSSelectorFromString(components[1]);
        BOOL isClassMethod = [methodName hasPrefix:@"+"];
        Method method = isClassMethod ? class_getClassMethod(class, name) : class_getInstanceMethod(class, name);
        addr = method_getImplementation(method);
        if (addr == _objc_msgForward) return NULL;
    }
    return addr;
}

- (void*)nextSymbol:(void*)address {
    Dl_info info, info2;
    if (dladdr(address, &info)) {
        for (void *next = address+2; dladdr(next, &info2) && info2.dli_saddr == info.dli_saddr; next += 2) {}
        return info2.dli_saddr;
    }
    return NULL;
}

- (NSString*)disassembleMethod:(void*)startAddress cpuType:(cpu_type_t)cpuType score:(NSInteger*)score {
    uint64_t endAddress = (uint64_t)[self nextSymbol:startAddress];
    if (endAddress == 0) {
        return @"Could not find end address";
    }
    uint64_t address = (uint64_t)startAddress;
    uint64_t codeSize = endAddress - address;
    NSData *code = [NSData dataWithBytesNoCopy:startAddress length:codeSize freeWhenDone:NO];
    
    AAHDisassembler *disassembler = [[AAHDisassembler alloc] initWithCode:code address:address cpuType:cpuType];
    [disassembler run];
    if (score) {
        *score = disassembler.score;
    }
    return disassembler.stringValue;
}

@end
