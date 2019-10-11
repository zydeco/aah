//
//  AAHCodeBrowser.h
//  TestApp
//
//  Created by Jesús A. Álvarez on 2019-10-06.
//  Copyright © 2019 namedfork. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <mach/machine.h>

NS_ASSUME_NONNULL_BEGIN

@interface AAHCodeBrowser : NSObject

- (void*)findMethodWithName:(NSString*)methodName;
- (NSString*)disassembleMethod:(void*)startAddress cpuType:(cpu_type_t)cpuType score:(NSInteger*)score;

@end

NS_ASSUME_NONNULL_END
