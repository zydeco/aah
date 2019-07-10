#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <dlfcn.h>


int dyld_get_active_platform(void);

int my_dyld_get_active_platform() {
    return 6;
}

void *my_Block_copy(const void *arg);

typedef struct interpose_s { void *new_func; void *orig_func; } interpose_t;
static const interpose_t interposing_functions[] __attribute__ ((used, section("__DATA, __interpose"))) = {
    { (void*) my_Block_copy, (void*)_Block_copy},
    //{ (void *)my_dyld_get_active_platform, (void *)dyld_get_active_platform }
};

@implementation NSBundle (Marzipan)
+(NSString *)currentStringsTableName { return nil; }
@end

@implementation NSObject (Marzipan)
-(CGFloat)_bodyLeading { return 0.0; }
@end
