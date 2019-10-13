//
//  AAHDisassembler.m
//  TestApp
//
//  Created by Jesús A. Álvarez on 2019-10-09.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import "AAHDisassembler.h"
#import <capstone/capstone.h>
#import <mach-o/getsect.h>
#import <mach-o/dyld.h>
#import <dlfcn.h>

static bool isJump(csh capstone, cs_insn *ins, cs_arch arch) {
    bool jump = (cs_insn_group(capstone, ins, CS_GRP_JUMP) ||
                 cs_insn_group(capstone, ins, CS_GRP_BRANCH_RELATIVE)) &&
                !cs_insn_group(capstone, ins, CS_GRP_CALL);
    // capstone doesn't have the right groups for pointer authenticated jumps
    if (jump == false && arch == CS_ARCH_ARM64 && ins->detail && ins->detail->groups_count == 0 &&
        (ins->id == ARM64_INS_BLRAA ||
         ins->id == ARM64_INS_BLRAAZ ||
         ins->id == ARM64_INS_BLRAB ||
         ins->id == ARM64_INS_BLRABZ ||
         ins->id == ARM64_INS_BRAA ||
         ins->id == ARM64_INS_BRAAZ ||
         ins->id == ARM64_INS_BRAB ||
         ins->id == ARM64_INS_BRABZ)) {
        return true;
    }
    return jump;
}

static bool isConditionalJump(csh capstone, cs_insn *ins, cs_arch arch) {
    if (arch == CS_ARCH_ARM64) {
        arm64_cc cc = ins->detail->arm64.cc;
        return cc >= ARM64_CC_EQ && cc <= ARM64_CC_LE;
    } else if (arch == CS_ARCH_X86) {
        return (ins->mnemonic[0] == 'j' && ins->id != X86_INS_JMP && ins->id != X86_INS_LJMP) ||
            ins->id == X86_INS_LOOP ||
            ins->id == X86_INS_LOOPE ||
            ins->id == X86_INS_LOOPNE;
    }
    return false;
}

static uint64_t targetForJump(csh capstone, cs_insn *ins, cs_arch arch) {
    // won't work for switches
    int index = cs_op_index(capstone, ins, CS_OP_IMM, 1);
    switch (arch) {
        case CS_ARCH_ARM64:
            return ins->detail->arm64.operands[index].imm;
        case CS_ARCH_X86:
            return ins->detail->x86.operands[index].imm;
        default:
            return 0;
    }
}

static bool isProcEnd(csh capstone, cs_insn *ins, cs_arch arch) {
    bool ret = cs_insn_group(capstone, ins, CS_GRP_RET);
    // capstone doesn't have the right groups for pointer authenticated returns
    if (ret == false && arch == CS_ARCH_ARM64 &&
        (ins->id == ARM64_INS_RETAA ||
         ins->id == ARM64_INS_RETAB)) {
        return true;
    } else if (ret == false && arch == CS_ARCH_X86 &&
               (ins->id == X86_INS_UD0 ||
                ins->id == X86_INS_UD1 ||
                ins->id == X86_INS_UD2
                )) {
        return true;
    }
    return ret;
}

static bool isStub(uint64_t addr, const struct section_64 *stubs, uint64_t slide) {
    if (stubs == NULL) {
        return false;
    }
    uint64_t start = stubs->addr + slide;
    uint64_t end = stubs->addr + slide + stubs->size;
    return addr >= start && addr < end;
}

static uint64_t image_get_vmaddr_slide(const void *fbase) {
    uint32_t image_count = _dyld_image_count();
    for(uint32_t i=0; i < image_count; i++) {
        if (_dyld_get_image_header(i) == fbase) {
            return _dyld_get_image_vmaddr_slide(i);
        }
    }
    return 0;
}

static cs_insn* instructionAtAddress(cs_insn *ins, size_t count, uint64_t address) {
    return bsearch_b((const void*)address, ins, count, sizeof(cs_insn), (int(^)(const void*, const void*)) ^int(uint64_t addr, const cs_insn * i) {
        return (int)(addr - i->address);
    });
}

static BOOL allValidInstructions(cs_insn *start, cs_insn *end) {
    for(cs_insn *i=start; i <= end; i++) {
        if (i->id == 0) {
            return NO;
        }
    }
    return YES;
}

static NSString* NSStringFromCSArch(cs_arch arch) {
    switch (arch) {
        case CS_ARCH_ARM: return @"ARM";
        case CS_ARCH_ARM64: return @"ARM64";
        case CS_ARCH_MIPS: return @"MIPS";
        case CS_ARCH_X86: return @"X86";
        case CS_ARCH_PPC: return @"PowerPC";
        case CS_ARCH_SPARC: return @"Sparc";
        case CS_ARCH_SYSZ: return @"SystemZ";
        case CS_ARCH_XCORE: return @"XCore";
        case CS_ARCH_M68K: return @"68K";
        case CS_ARCH_TMS320C64X: return @"TMS320C64x";
        case CS_ARCH_M680X: return @"680X";
        case CS_ARCH_EVM: return @"Ethereum";
        case CS_ARCH_MOS65XX: return @"MOS65XX";
        case CS_ARCH_WASM: return @"WebAssembly";
        case CS_ARCH_BPF: return @"Berkeley Packet Filter";
        case CS_ARCH_RISCV: return @"RISCV";
        default: return [NSString stringWithFormat:@"%d", (int)arch];
    }
}

@implementation AAHDisassembler
{
    csh capstone;
    cs_err capstoneError;
    cs_insn *instructions;
    cs_arch arch;
    size_t instructionCount;
    NSMutableSet<NSNumber*> *blockStarts, *blockEnds, *instructionAddresses;
}

- (instancetype)initWithCode:(NSData *)code address:(uint64_t)address cpuType:(cpu_type_t)cpuType {
    if (cpuType != CPU_TYPE_ARM64 && cpuType != CPU_TYPE_X86_64) {
    }
    if ((self = [super init])) {
        _code = code;
        _startAddress = address;
        _endAddress = address + code.length;
        _cpuType = cpuType;
        _score = NSIntegerMin;
        _hasRun = NO;
        _stringValue = nil;
        [self configureCapstone];
        blockStarts = nil;
        blockEnds = nil;
        instructionAddresses = nil;
    }
    return self;
}

- (void)configureCapstone {
    cs_mode mode;
    switch (_cpuType) {
        case CPU_TYPE_X86_64:
            arch = CS_ARCH_X86;
            mode = CS_MODE_64;
            break;
            
        case CPU_TYPE_ARM64:
            arch = CS_ARCH_ARM64;
            mode = CS_MODE_LITTLE_ENDIAN;
            break;
        default:
            @throw [NSException exceptionWithName:NSInvalidArgumentException reason:@"Unsupported CPU type." userInfo:@{@"cpuType": @(_cpuType)}];
            break;
    }
    capstoneError = cs_open(arch, mode, &capstone);
    if (capstoneError) {
        _errorMessage = @(cs_strerror(capstoneError));
        return;
    }
    cs_option(capstone, CS_OPT_DETAIL, CS_OPT_ON);
    cs_option(capstone, CS_OPT_SKIPDATA, CS_OPT_ON);
    instructions = NULL;
}

- (void)dealloc {
    cs_free(instructions, instructionCount);
    cs_close(&capstone);
}

- (void)run {
    NSMutableString *disassembly = [self _runDisassembly];
    [self _analyzeControlFlow];
    [disassembly insertString:[NSString stringWithFormat:@"; %@ - score = %ld\n", NSStringFromCSArch(arch), (long)_score] atIndex:0];
    _hasRun = YES;
    _stringValue = disassembly.copy;
}


#define I(address) instructionAtAddress(instructions, instructionCount, address)

- (NSMutableString *)_runDisassembly {
    instructionCount = cs_disasm(capstone, _code.bytes, _code.length, _startAddress, 0, &instructions);
    
    Dl_info info;
    dladdr((void*)_startAddress, &info);
    const struct section_64 *stubs = getsectbynamefromheader_64(info.dli_fbase, SEG_TEXT, "__stubs");
    uint64_t slide = image_get_vmaddr_slide(info.dli_fbase);
    
    blockStarts = [NSMutableSet setWithObject:@(_startAddress)];
    blockEnds = [NSMutableSet new];
    instructionAddresses = [NSMutableSet setWithCapacity:instructionCount];
    _score = 0;
    
    if (instructionCount == 0) {
        _errorMessage = @(cs_strerror(cs_errno(capstone)));
        return nil;
    }
    
    // get instruction addreses
    for(size_t i=0; i < instructionCount; i++) {
        if (instructions[i].id != 0) {
            [instructionAddresses addObject:@(instructions[i].address)];
        }
    }

    // text disassembly
    NSMutableString *disassembly = [NSMutableString new];
    for(cs_insn *i=instructions; i < &instructions[instructionCount]; i++) {
        [disassembly appendFormat:@"0x%llx: %-12s%s", i->address, i->mnemonic, i->op_str];
        if (isJump(capstone, i, arch)) {
            uint64_t target = targetForJump(capstone, i, arch);
            if (target > _startAddress && target < _endAddress) {
                // jump within function
                [blockEnds addObject:@(i->address)];
                if ([instructionAddresses containsObject:@(target)]) {
                    // jump to another instruction
                    [blockStarts addObject:@(target)];
                    if (isConditionalJump(capstone, i, arch)) {
                        // next instruction starts block
                        [disassembly appendFormat:@" ; conditional jump"];
                        [blockStarts addObject:@(i->address + i->size)];
                    }
                    _score += 1;
                } else {
                    // probably a bad jump
                    [disassembly appendFormat:@" ; mid-instruction jump"];
                    _score -= 200;
                }
            } else if (isStub(target, stubs, slide)) {
                // stub -- not considered to interrupt flow
                [disassembly appendString:@" ; stub"];
                _score += 10;
            } else {
                // jump somewhere else
                [blockEnds addObject:@(i->address)];
                [blockStarts addObject:@(i->address + i->size)];
                [disassembly appendString:@" ; unknown"];
                if (target != 0) {
                    _score -= 1;
                }
            }
        } else if (isProcEnd(capstone, i, arch)) {
            [blockEnds addObject:@(i->address)];
            [disassembly appendString:@" ; endp"];
        }
        [disassembly appendString:@"\n"];
    }
    
    // basic block formatting
    NSUInteger loc = 1;
    NSArray<NSNumber*> *sortedBlockStarts = [blockStarts.allObjects sortedArrayUsingSelector:@selector(compare:)];
    for (NSNumber *addr in sortedBlockStarts) {
        // annotate blocks with labels
        if (addr.unsignedLongValue == _startAddress) continue;
        NSString *str = [NSString stringWithFormat:@"0x%llx:", addr.unsignedLongLongValue];
        NSRange range = [disassembly rangeOfString:str];
        if (range.location != NSNotFound) {
            NSString *label = [NSString stringWithFormat:@"loc_%lu", (unsigned long)loc++];
            [disassembly insertString:label atIndex:range.location];
            [disassembly insertString:@":\n" atIndex:range.location+label.length];
            NSString *addrString = [NSString stringWithFormat:@"0x%llx", addr.unsignedLongLongValue];
            [disassembly replaceOccurrencesOfString:addrString withString:label options:NSLiteralSearch range:NSMakeRange(range.location+label.length+3, disassembly.length-(range.location+label.length+3))];
            [disassembly replaceOccurrencesOfString:addrString withString:label options:NSLiteralSearch range:NSMakeRange(0, range.location)];
        }
        // add block end before this instruction
        cs_insn *i = I(addr.unsignedLongLongValue);
        if (i) {
            [blockEnds addObject:@(i[-1].address)];
        }
    }
    for (NSNumber *addr in blockEnds) {
        // mark block endings
        NSString *str = [NSString stringWithFormat:@"0x%llx:", addr.unsignedLongLongValue];
        NSRange range = [disassembly rangeOfString:str];
        range.length = disassembly.length - range.location;
        range = [disassembly rangeOfString:@"\n" options:NSLiteralSearch range:range];
        [disassembly insertString:@"---\n" atIndex:range.location+1];
    }
    
    return disassembly;
}

- (void)_analyzeControlFlow {
    if (blockStarts.count == 1 && blockEnds.count == 1) {
        // single block function ending in return
        cs_insn *firstInstruction = I(_startAddress);
        cs_insn *lastInstruction = I(blockEnds.anyObject.unsignedLongLongValue);
        if (lastInstruction && isProcEnd(capstone, lastInstruction, arch)) {
            _score += 500;
            if (allValidInstructions(firstInstruction, lastInstruction)) {
                _score += 500;
            }
        }
    } else {
        // analyze control flow
        NSMutableSet<NSNumber*> *seenBlocks = [NSMutableSet new];
        NSMutableArray<NSNumber*> *blocksToSee = [NSMutableArray arrayWithObject:@(_startAddress)];
        BOOL returns = NO;
        while(blocksToSee.count) {
            NSNumber *nextBlock = blocksToSee[0];
            [blocksToSee removeObjectAtIndex:0];
            if ([seenBlocks containsObject:nextBlock]) {
                continue;
            }
            [seenBlocks addObject:nextBlock];
            NSArray<NSNumber*> *nextBlocks = [self _analyzeBlock:nextBlock.unsignedLongLongValue];
            if (nextBlocks == nil) {
                returns = YES;
            } else {
                [blocksToSee addObjectsFromArray:nextBlocks];
            }
        }
        if (returns) {
            _score += 200;
        }
    }
}

- (NSArray<NSNumber*>*)_analyzeBlock:(uint64_t)address {
    cs_insn *i = I(address);
    if (i == NULL) {
        // not in function
        return @[];
    }
    
    do {
        if (i->id == 0) {
            // block shouldn't have undefined instructions
            _score -= 50 * i->size;
        }
        i++;
        if (i == &instructions[instructionCount]) {
            // out of range
            return @[];
        }
    } while (![blockEnds containsObject:@(i->address)]);
    
    // last instruction
    NSMutableArray<NSNumber*> *nextBlocks = [NSMutableArray new];
    if (isJump(capstone, i, arch)) {
        if (isConditionalJump(capstone, i, arch)) {
            [nextBlocks addObject:@((i+1)->address)];
        }
        uint64_t target = targetForJump(capstone, i, arch);
        if (target && target >= _startAddress && target < _endAddress) {
            [nextBlocks addObject:@(target)];
        }
    } else if (isProcEnd(capstone, i, arch)) {
        // return
        return nil;
    } else {
        // fall through
        [nextBlocks addObject:@((i+1)->address)];
    }
    return nextBlocks;
}

@end
