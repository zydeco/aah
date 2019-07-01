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
    uc_hook instr_hook;
};

hidden struct emulator_ctx* init_emulator_ctx(void);
hidden struct emulator_ctx* get_emulator_ctx(void);
hidden uc_engine* get_unicorn(void);
hidden void run_emulator(struct emulator_ctx *ctx, uint64_t start_address);
hidden void print_disasm(struct emulator_ctx *ctx, int print);

#define MH_MAGIC_EMULATED 0x456D400C

static inline int should_emulate_image(const struct mach_header_64 *mh) {
    return mh->magic == MH_MAGIC_64 && mh->reserved == MH_MAGIC_EMULATED;
}

hidden bool mem_map_region_containing(uc_engine *uc, uint64_t address, uint32_t perms);
hidden void print_region_info(void *ptr);

#define WRAPPER_ARGS (void *rvalue, void **avalues)
typedef uint64_t (*wrapper_ptr)WRAPPER_ARGS;

struct call_wrapper {
    ffi_cif *cif_native;
    ffi_cif_arm64 *cif_arm64;
    wrapper_ptr emulated_to_native, native_to_emulated;
};

struct native_call_context {
    ffi_cif_arm64 *cif_arm64;
    ffi_cif *cif_native;
    wrapper_ptr before, after;
    uint64_t pc, sp;
    struct arm64_call_context *arm64_call_context;
};

typedef uint64_t (*shim_ptr)(uc_engine*, struct native_call_context*);

hidden void init_cif (void);
hidden void cif_cache_add_new(void *address, const char *method_signature); // doesn't overwrite
hidden void cif_cache_add(void *address, const char *method_signature); // overwrites
hidden void load_objc_entrypoints(void);
hidden void* resolve_symbol(const char *libname, const char *symname);
hidden void cif_cache_add_class(const char *className);
hidden ffi_cif * cif_cache_get_native(void *address);
hidden ffi_cif_arm64 * cif_cache_get_arm64(void *address);
hidden uint64_t call_native(uc_engine *uc, uint64_t pc);
hidden void call_native_with_context(uc_engine *uc, struct native_call_context *ctx);
hidden void call_emulated_function (ffi_cif *cif, void *ret, void **args, void *address);
hidden const char * lookup_method_signature(const char *lib_name, const char *sym_name);
// fixed_args is # of fixed args in variadic functions, -1 otherwise
hidden int prep_cifs(ffi_cif *cif, ffi_cif_arm64 *cif_arm64, const char *method_signature, int fixed_args);
extern const char *CIF_LIB_OBJC_SHIMS;

#define CIF_MARKER_SHIM ((ffi_cif *)1)
#define CIF_MARKER_WRAPPER ((ffi_cif *)2)
#define CIF_IS_CIF(cif) (((uint64_t)cif) > 15)

#define SHIM_RETURN 0
#define SHIMDEF(name) __attribute__((visibility("default"))) uint64_t aah_shim_ ## name (uc_engine *uc, struct native_call_context *ctx)
#define WRAP_EMULATED_TO_NATIVE(name) __attribute__((visibility("default"))) void aah_We2n_ ## name WRAPPER_ARGS
#define WRAP_NATIVE_TO_EMULATED(name) __attribute__((visibility("default"))) void aah_Wn2e_ ## name WRAPPER_ARGS
