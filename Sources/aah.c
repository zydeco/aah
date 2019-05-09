#include "aah.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

__attribute__((constructor)) static void init_aah(void) {
    // initialize unicorn
    unsigned int maj, min;
    uc_version(&maj, &min);
    printf("Unicorn version %d.%d\n", maj, min);
    
    get_unicorn();
    init_cif();
    init_loader();
    
    // set up signal handler
    struct sigaction sa, osa;
    sa.sa_flags = SA_ONSTACK | SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = sighandler;
    sigaction(SIGBUS, &sa, &osa);
}

hidden void sighandler (int signo, siginfo_t *si, void *data) {
    ucontext_t *uctx = (ucontext_t *)data;
    mcontext_t mc = uctx->uc_mcontext;
    uint64_t pc = mc->__ss.__rip;
    Dl_info info;
    printf("bus error at %p\n", (void*)pc);
    if (dladdr((void*)pc, &info) && should_emulate_image((const struct mach_header_64*)info.dli_fbase)) {
        // get native cif for call
        ffi_cif *cif = cif_cache_get_native((void*)pc);
        // TODO: shim? objc method?
        
        if (cif == NULL && info.dli_sname && info.dli_sname[1] == '[') {
            // calling unmarked obj-c method
            if (strcmp(strchr(info.dli_sname, ']') - 5, " load]") == 0) {
                // calling load method
                cif_cache_add((void*)pc, "v");
            } else {
                // load all methods for class
                char *buf = strdup(info.dli_sname);
                strchr(buf, ' ')[0] = '\0';
                const char *className = &buf[2];
                cif_cache_add_class(className);
                free(buf);
            }
            cif = cif_cache_get_native((void*)pc);
        }
        
        static int loaded_objc = 0;
        if (cif == NULL && loaded_objc == 0) {
            load_objc_entrypoints();
            loaded_objc = 1;
            cif = cif_cache_get_native((void*)pc);
        }
        
        if (cif) {
            // call arm64 entry point
            printf("calling emulated %s at %p\n", info.dli_sname ?: "(unnamed function)", (void*)pc);
            struct emulator_ctx *ctx = get_emulator_ctx();
            if (ffi_prep_closure_loc(ctx->closure, cif, call_emulated_function, (void*)pc, ctx->closure_code) != FFI_OK) {
                fprintf(stderr, "ffi_prep_closure_loc failed\n");
                abort();
            }
            mc->__ss.__rip = (uint64_t)ctx->closure_code;
        } else {
            // TODO: when is this? maybe blocks or callbacks
            printf("returning to emulated %s at %p (+%lld)\n", info.dli_sname, (void*)pc, pc - (uint64_t)info.dli_saddr);
            abort();
        }
    } else {
        // pc not in emulatable image
        fprintf(stderr, "pc not in emulatable image (%p)\n", (void*)pc);
        abort();
    }
}
