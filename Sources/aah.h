#include <inttypes.h>
#include <dlfcn.h>
#include <errno.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/nlist.h>
#include <mach/vm_map.h>
#include <mach/mach_init.h>
#include <unicorn/unicorn.h>
#include <capstone/capstone.h>
#include <ffi.h>

#define hidden __attribute__ ((visibility ("hidden")))

#include "ffi_arm64.h"

hidden void init_loader (void);

void sighandler (int signo, siginfo_t *si, void *data);

struct emulator_ctx {
    uc_engine *uc;
    size_t stack_size;
    size_t pagezero_size;
    void *stack;
    uint64_t return_ptr;
    csh capstone;
    cs_insn insn;
    void *closure;
    void *closure_code;
};

hidden struct emulator_ctx* init_emulator_ctx();
hidden struct emulator_ctx* get_emulator_ctx();
hidden uc_engine* get_unicorn();
hidden void run_emulator(struct emulator_ctx *ctx, uint64_t start_address);

#define MH_MAGIC_EMULATED 0x456D400C

static inline int should_emulate_image(const struct mach_header_64 *mh) {
    return mh->magic == MH_MAGIC_64 && mh->reserved == MH_MAGIC_EMULATED;
}

hidden bool mem_map_region_containing(uc_engine *uc, uint64_t address, uint32_t perms);
hidden void print_region_info(void *ptr);

struct native_call_context {
    ffi_cif_arm64 *cif_arm64;
    ffi_cif *cif_native;
    uint64_t pc, sp;
    struct arm64_call_context *arm64_call_context;
};

typedef void (*shim_ptr)(uc_engine*, struct native_call_context*);

hidden void init_cif (void);
hidden void cif_cache_add(void *address, const char *method_signature);
hidden ffi_cif * cif_cache_get_native(void *address);
hidden ffi_cif_arm64 * cif_cache_get_arm64(void *address);
hidden void call_native(uc_engine *uc, uint64_t pc);
hidden void call_native_with_context(uc_engine *uc, struct native_call_context *ctx);
hidden void call_emulated_function (ffi_cif *cif, void *ret, void **args, void *address);
hidden const char * lookup_method_signature(const char *lib_name, const char *sym_name);
hidden int prep_cifs(ffi_cif *cif, ffi_cif_arm64 *cif_arm64, const char *method_signature, int fixed_args);

#define SHIMDEF(name) __attribute__((visibility("default"))) void oah_shim_ ## name (uc_engine *uc, struct native_call_context *ctx)
