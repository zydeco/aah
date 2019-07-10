#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <dlfcn.h>
#import <pthread.h>
#import "aah.h"

void *my_Block_copy(const void *arg);

int aah_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
    printf("caching cif for pthread start routine %p\n", start_routine);
    cif_cache_add(start_routine, "??", "(start_routine for a pthread)");
    return pthread_create(thread, attr, start_routine, arg);
};

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int aah_pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    if (destructor != NULL) {
        printf("caching cif for pthread tsd destructor %p\n", destructor);
        cif_cache_add(destructor, "v?", "(destructor for thread-specific-data)");
    }
    return pthread_key_create(key, destructor);
}

typedef struct interpose_s { void *new_func; void *orig_func; } interpose_t;
static const interpose_t interposing_functions[] __attribute__ ((used, section("__DATA, __interpose"))) = {
    { (void*) my_Block_copy, (void*)_Block_copy},
    { (void*) aah_pthread_create, (void*)pthread_create},
    { (void*) aah_pthread_key_create, (void*)pthread_key_create},
};

@implementation NSBundle (Marzipan)
+(NSString *)currentStringsTableName { return nil; }
@end

@implementation NSObject (Marzipan)
-(CGFloat)_bodyLeading { return 0.0; }
@end
