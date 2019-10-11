//
//  AAHDisassembler.h
//  TestApp
//
//  Created by Jesús A. Álvarez on 2019-10-09.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <mach/machine.h>

NS_ASSUME_NONNULL_BEGIN

@interface AAHDisassembler : NSObject

@property (nonatomic, readonly) NSData *code;
@property (nonatomic, readonly) uint64_t startAddress;
@property (nonatomic, readonly) uint64_t endAddress;
@property (nonatomic, readonly) cpu_type_t cpuType;

@property (nonatomic, readonly) NSString *errorMessage;
@property (nonatomic, readonly) BOOL hasRun;
@property (nonatomic, readonly) NSInteger score;
@property (nonatomic, readonly) NSString *stringValue;

- (instancetype)initWithCode:(NSData*)code address:(uint64_t)address cpuType:(cpu_type_t)cpuType;
- (void)run;

@end

NS_ASSUME_NONNULL_END
