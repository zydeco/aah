#include "aah.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <objc/runtime.h>

uint64_t loadSelector;

__attribute__((constructor)) static void init_aah(void) {
    // initialize unicorn
    unsigned int maj, min;
    uc_version(&maj, &min);
    printf("Unicorn version %d.%d\n", maj, min);
    
    init_emulator_ctx_key();
    get_emulator_ctx();
    init_cif();
    init_loader();
    
    loadSelector = (uint64_t)sel_registerName("load");
    
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
    //printf("bus error at %p\n", (void*)pc);
    uint32_t should_emulate = should_emulate_at(pc);
    if (should_emulate) {
        // get native cif for call
        ffi_cif *cif = cif_cache_get_native((void*)pc);
        
        if (cif == CIF_MARKER_WRAPPER) {
            struct call_wrapper *wrapper = (struct call_wrapper*)cif_cache_get_arm64((void*)pc);
            cif = wrapper->cif_native;
        } else if (cif == CIF_MARKER_SHIM) {
            abort();
        } else if (cif == NULL && mc->__ss.__rsi == loadSelector) {
            // calling unknown load method
            cif_cache_add((void*)pc, "v@:", "+[??? load]");
            cif = cif_cache_get_native((void*)pc);
        }
        
        if (cif) {
            // call arm64 entry point
            printf("calling emulated %s at %p\n", cif_get_name((void*)pc), (void*)pc);
            struct emulator_ctx *ctx = get_emulator_ctx();
            if (ffi_prep_closure_loc(ctx->closure, cif, call_emulated_function, (void*)pc, ctx->closure_code) != FFI_OK) {
                fprintf(stderr, "ffi_prep_closure_loc failed\n");
                abort();
            }
            mc->__ss.__rip = (uint64_t)ctx->closure_code;
        } else if (should_emulate & AAH_RANGE_LIBCPP) {
            // FIXME: loading emulated libc++ messes up the namespace
            dladdr((void*)pc, &info);
            mc->__ss.__rip = (uint64_t)resolve_symbol("/usr/lib/libc++.1.dylib", info.dli_sname);
        } else {
            // TODO: when is this? maybe blocks or callbacks
            dladdr((void*)pc, &info);
            printf("returning to unknown emulated %p (%s+0x%llx:%s)\n", (void*)pc, info.dli_fname, pc - (uint64_t)info.dli_fbase, info.dli_sname);
            abort();
        }
    } else {
        // pc not in emulatable image
        dladdr((void*)pc, &info);
        fprintf(stderr, "pc not in emulatable image at %p (%s+0x%llx:%s)\n", (void*)pc, info.dli_fname, pc - (uint64_t)info.dli_fbase, info.dli_sname);
        abort();
    }
}
