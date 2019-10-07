//
//  AAHCodeBrowser.m
//  TestApp
//
//  Created by Jesús A. Álvarez on 2019-10-06.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import "AAHCodeBrowser.h"
#import <dlfcn.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <capstone/capstone.h>

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

- (NSString*)disassembleCode:(NSData*)code address:(uint64_t)address architecture:(cs_arch)arch mode:(cs_mode)mode skippedBytes:(size_t*)skippedBytesPtr {
    csh capstone;
    cs_err err;
    err = cs_open(arch, mode, &capstone);
    cs_option(capstone, CS_OPT_SKIPDATA, CS_OPT_ON);
    cs_insn *insn = NULL;
    
    size_t count = cs_disasm(capstone, code.bytes, code.length, address, 0, &insn);
    cs_close(&capstone);
    
    size_t skippedBytes = 0, skippedBlock = 0;
    if (count) {
        NSMutableString *disassembly = [NSMutableString new];
        for(size_t i=0; i < count; i++) {
            [disassembly appendFormat:@"0x%llx:\t%-12s%s\n", insn[i].address, insn[i].mnemonic, insn[i].op_str];
            if (insn[i].id == 0) {
                // skipped data
                skippedBytes += insn[i].size;
                skippedBlock += insn[i].size;
                if (i == count-1) {
                    // don't count trailing skip
                    skippedBlock -= skippedBlock;
                }
            } else {
                skippedBlock = 0;
            }
        }
        cs_free(insn, count);
        if (skippedBytesPtr) {
            *skippedBytesPtr = skippedBytes;
        }
        return disassembly;
    }
    
    return nil;
}

- (NSString*)disassembleMethod:(void*)startAddress cpuType:(cpu_type_t)cpuType{
    uint64_t endAddress = (uint64_t)[self nextSymbol:startAddress];
    if (endAddress == 0) {
        return @"Could not find end address";
    }
    uint64_t address = (uint64_t)startAddress;
    uint64_t codeSize = endAddress - address;
    NSData *code = [NSData dataWithBytesNoCopy:startAddress length:codeSize freeWhenDone:NO];
    
    if (cpuType == CPU_TYPE_X86_64) {
        return [self disassembleCode:code address:address architecture:CS_ARCH_X86 mode:CS_MODE_64 skippedBytes:NULL];
    } else if (cpuType == CPU_TYPE_ARM64) {
        return [self disassembleCode:code address:address architecture:CS_ARCH_ARM64 mode:CS_MODE_LITTLE_ENDIAN skippedBytes:NULL];
    } else {
        @throw [NSException exceptionWithName:NSInvalidArgumentException reason:@"Unsupported CPU type" userInfo:@{@"cpuType": @(cpuType)}];
    }
}

@end
