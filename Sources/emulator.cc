extern "C" {
    #include "aah.h"
    #include <pthread.h>
}
#include <exception>

static bool cb_invalid_rw(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, struct emulator_ctx *ctx);
static bool cb_invalid_fetch(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, struct emulator_ctx *ctx);
static bool cb_print_disasm(uc_engine *uc, uint64_t address, uint32_t size, struct emulator_ctx *ctx);
static void destroy_emulator_ctx(void *ptr);

static pthread_key_t emulator_ctx_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

static void dont_print_regs(uc_engine *uc,int) {};
static void(*maybe_print_regs)(uc_engine*,int) = dont_print_regs;

static void print_regs(uc_engine *uc, int print_all) {
    uint64_t x[32], pc, sp, lr, fp;
    uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
    uc_reg_read(uc, UC_ARM64_REG_SP, &sp);
    uc_reg_read(uc, UC_ARM64_REG_FP, &fp);
    uc_reg_read(uc, UC_ARM64_REG_LR, &lr);
    uc_reg_read(uc, UC_ARM64_REG_X0, &x[0]);
    uc_reg_read(uc, UC_ARM64_REG_X1, &x[1]);
    uc_reg_read(uc, UC_ARM64_REG_X2, &x[2]);
    uc_reg_read(uc, UC_ARM64_REG_X3, &x[3]);
    uc_reg_read(uc, UC_ARM64_REG_X4, &x[4]);
    uc_reg_read(uc, UC_ARM64_REG_X5, &x[5]);
    uc_reg_read(uc, UC_ARM64_REG_X6, &x[6]);
    uc_reg_read(uc, UC_ARM64_REG_X7, &x[7]);
    uc_reg_read(uc, UC_ARM64_REG_X8, &x[8]);
    if (print_all) {
        uc_reg_read(uc, UC_ARM64_REG_X9, &x[9]);
        uc_reg_read(uc, UC_ARM64_REG_X10, &x[10]);
        uc_reg_read(uc, UC_ARM64_REG_X11, &x[11]);
        uc_reg_read(uc, UC_ARM64_REG_X12, &x[12]);
        uc_reg_read(uc, UC_ARM64_REG_X13, &x[13]);
        uc_reg_read(uc, UC_ARM64_REG_X14, &x[14]);
        uc_reg_read(uc, UC_ARM64_REG_X15, &x[15]);
        uc_reg_read(uc, UC_ARM64_REG_X16, &x[16]);
        uc_reg_read(uc, UC_ARM64_REG_X17, &x[17]);
        uc_reg_read(uc, UC_ARM64_REG_X18, &x[18]);
        uc_reg_read(uc, UC_ARM64_REG_X19, &x[19]);
        uc_reg_read(uc, UC_ARM64_REG_X20, &x[20]);
        uc_reg_read(uc, UC_ARM64_REG_X21, &x[21]);
        uc_reg_read(uc, UC_ARM64_REG_X22, &x[22]);
        uc_reg_read(uc, UC_ARM64_REG_X23, &x[23]);
        uc_reg_read(uc, UC_ARM64_REG_X24, &x[24]);
        uc_reg_read(uc, UC_ARM64_REG_X25, &x[25]);
        uc_reg_read(uc, UC_ARM64_REG_X26, &x[26]);
        uc_reg_read(uc, UC_ARM64_REG_X27, &x[27]);
        uc_reg_read(uc, UC_ARM64_REG_X28, &x[28]);
        uc_reg_read(uc, UC_ARM64_REG_X29, &x[29]);
        uc_reg_read(uc, UC_ARM64_REG_X30, &x[30]);
        /*uc_reg_read(uc, UC_ARM64_REG_V0, &v[0]);
        uc_reg_read(uc, UC_ARM64_REG_V1, &v[1]);
        uc_reg_read(uc, UC_ARM64_REG_V2, &v[2]);
        uc_reg_read(uc, UC_ARM64_REG_V3, &v[3]);
        uc_reg_read(uc, UC_ARM64_REG_V4, &v[4]);
        uc_reg_read(uc, UC_ARM64_REG_V5, &v[5]);
        uc_reg_read(uc, UC_ARM64_REG_V6, &v[6]);
        uc_reg_read(uc, UC_ARM64_REG_V7, &v[7]);*/
    }
    
    int last_reg = print_all ? 30 : 8;
    for (int i=0; i <=last_reg; i++) {
        printf("x%d:0x%016llx ", i, x[i]);
        if (i % 4 == 3) {
            printf("\n");
        }
    }
    printf("\n");
    printf("pc:0x%016llx ", pc);
    printf("sp:0x%016llx ", sp);
    printf("fp:0x%016llx ", fp);
    printf("lr:0x%016llx\n", lr);
}

hidden struct emulator_ctx* init_emulator_ctx() {
    struct emulator_ctx *ctx = (struct emulator_ctx*)calloc(1, sizeof(struct emulator_ctx));
    pthread_setspecific(emulator_ctx_key, ctx);
    uc_err err;
    printf("init unicorn\n");
    // initialize unicorn
    err = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &ctx->uc);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "uc_open: %u %s\n", err, uc_strerror(err));
        abort();
    }
    
    // catch invalid memory access
    uc_hook mem_hook;
    err = uc_hook_add(ctx->uc, &mem_hook, UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED, (void*)cb_invalid_rw, ctx, 8, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "uc_hook_add: %u %s\n", err, uc_strerror(err));
        abort();
    }
    
    // catch invalid execute
    uc_hook exe_hook;
    err = uc_hook_add(ctx->uc, &exe_hook, UC_HOOK_MEM_FETCH_PROT | UC_HOOK_MEM_FETCH_UNMAPPED, (void*)cb_invalid_fetch, ctx, 8, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "uc_hook_add: %u %s\n", err, uc_strerror(err));
        abort();
    }
    
    if (getenv("PRINT_DISASSEMBLY") && strtol(getenv("PRINT_DISASSEMBLY"), NULL, 10)) {
        print_disasm(ctx, 1);
    } else {
        ctx->instr_hook = 0;
    }
    
    if (getenv("PRINT_REGS") && strtol(getenv("PRINT_REGS"), NULL, 10)) {
        // print some registers sometimes
        maybe_print_regs = print_regs;
    }
    
    // map memory for stack
    ctx->stack_size = pthread_get_stacksize_np(pthread_self());
    ctx->stack = malloc(ctx->stack_size);
    uint64_t stack_top = ((uint64_t)ctx->stack) + ctx->stack_size;
    printf("Emulated stack is %p to %p\n", ctx->stack, (void*)stack_top);
    uc_reg_write(ctx->uc, UC_ARM64_REG_SP, &stack_top);
    ctx->return_ptr = (uint64_t)ctx;
    ctx->pagezero_size = getsegbyname(SEG_PAGEZERO)->vmsize;
    printf("Page zero is 0x%lx\n", ctx->pagezero_size);
    
    // enable FPU
    uint32_t cpacr_el1;
    uc_reg_read(ctx->uc, UC_ARM64_REG_CPACR_EL1, &cpacr_el1);
    cpacr_el1 |= (0x3 << 20);
    uc_reg_write(ctx->uc, UC_ARM64_REG_CPACR_EL1, &cpacr_el1);
    
    // closure for interworking
    ctx->closure = ffi_closure_alloc(sizeof(ffi_closure), &ctx->closure_code);
    // TODO: check reentrancy thoroughly, seems ok so far
    return ctx;
}

static void init_key() {
    pthread_key_create(&emulator_ctx_key, destroy_emulator_ctx);
}

hidden uc_engine* get_unicorn() {
    pthread_once(&key_once, init_key);
    struct emulator_ctx *ctx = (struct emulator_ctx*)pthread_getspecific(emulator_ctx_key);
    if (ctx == NULL) ctx = init_emulator_ctx();
    return ctx->uc;
}

hidden struct emulator_ctx* get_emulator_ctx() {
    return (struct emulator_ctx*)pthread_getspecific(emulator_ctx_key);
}

hidden void print_disasm(struct emulator_ctx *ctx, int print) {
    if (print && ctx->instr_hook == 0) {
        // print emulated instructions as they are fetched
        cs_err cerr = cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &(ctx->capstone));
        if (cerr != CS_ERR_OK) {
            fprintf(stderr, "cs_open: %s\n", cs_strerror(cerr));
            abort();
        }
        uc_err err = uc_hook_add(ctx->uc, &ctx->instr_hook, UC_HOOK_CODE, (void*)cb_print_disasm, ctx, 8, 0);
        if (err != UC_ERR_OK) {
            fprintf(stderr, "uc_hook_add: %u %s\n", err, uc_strerror(err));
            abort();
        }
    } else if (print == 0 && ctx->instr_hook) {
        uc_hook_del(ctx->uc, ctx->instr_hook);
        ctx->instr_hook == 0;
    }
}

static void destroy_emulator_ctx(void *ptr) {
    struct emulator_ctx *ctx = (struct emulator_ctx *)ptr;
    // TODO: is it running?
    uc_free(ctx->uc);
    free(ctx->stack);
    cs_close(&ctx->capstone);
    ffi_closure_free(ctx->closure);
    pthread_setspecific(emulator_ctx_key, NULL);
}

void run_emulator(struct emulator_ctx *ctx, uint64_t start_address) {
    printf("running emulator at %p\n", (void*)start_address);
    maybe_print_regs(ctx->uc, 1);
    
    uc_engine *uc = ctx->uc;
    uint64_t pc;
    uc_err err;
    Dl_info info;
    uc_reg_write(uc, UC_ARM64_REG_LR, &ctx->return_ptr);
    for(;;) {
        err = uc_emu_start(uc, start_address, ctx->return_ptr, 0, 0);
        uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
        if (pc == ctx->return_ptr) {
            printf("emulation finished ok?\n");
            return;
        } else if (err == UC_ERR_FETCH_PROT && dladdr((void*)pc, &info) && !should_emulate_image((struct mach_header_64 *)info.dli_fbase)) {
            uint64_t last_lr;
            uc_reg_read(uc, UC_ARM64_REG_LR, &last_lr);
            maybe_print_regs(uc, 0);
            printf("  calling native %s from %p\n", info.dli_sname, (void*)last_lr);
            try {
                start_address = call_native(uc, pc);
            }
            catch (const std::exception& e) {
                // find catch block
            }
            if (start_address == 0) {
                start_address = last_lr;
            }
            //print_regs(uc, 0);
            continue;
        } else {
            // could be a c++ virtual method, since it can be called without being linked
            if (pc) {
                dladdr((void*)pc, &info);
            } else {
                info.dli_sname = NULL;
                info.dli_fname = NULL;
                info.dli_fbase = 0;
            }
            printf("emulation finished badly at %p (%s+0x%llx:%s): %s\n", (void*)pc, info.dli_fname, pc ? pc - (uint64_t)info.dli_fbase : 0, info.dli_sname, uc_strerror(err));
            abort();
        }
    };
}

static inline const char * uc_mem_type_to_string(uc_mem_type type) {
    switch (type) {
        case UC_MEM_READ: return "UC_MEM_READ";
        case UC_MEM_WRITE: return "UC_MEM_WRITE";
        case UC_MEM_FETCH: return "UC_MEM_FETCH";
        case UC_MEM_READ_UNMAPPED: return "UC_MEM_READ_UNMAPPED";
        case UC_MEM_WRITE_UNMAPPED: return "UC_MEM_WRITE_UNMAPPED";
        case UC_MEM_FETCH_UNMAPPED: return "UC_MEM_FETCH_UNMAPPED";
        case UC_MEM_WRITE_PROT: return "UC_MEM_WRITE_PROT";
        case UC_MEM_READ_PROT: return "UC_MEM_READ_PROT";
        case UC_MEM_FETCH_PROT: return "UC_MEM_FETCH_PROT";
        case UC_MEM_READ_AFTER: return "UC_MEM_READ_AFTER";
        default: return "UNKNOWN";
    }
}

void print_regions(uc_engine *uc) {
    uc_mem_region *regions;
    uint32_t count;
    uc_mem_regions(uc, &regions, &count);
    printf("regions:\n");
    static const char* prots[] = {
        "none",
        "r",
        "w",
        "rw",
        "x",
        "rx",
        "wx",
        "rwx"
    };
    for (uint32_t i=0; i < count; i++) {
        const char *tag = "";
        if (regions[i].perms & UC_PROT_READ) {
            uint32_t header = *(uint32_t*)regions[i].begin;
            Dl_info info;
            if (header == MH_MAGIC_64 && dladdr((void*)regions[i].begin, &info)) {
                tag = strrchr(info.dli_fname, '/') + 1;
            }
        }
        printf("  0x%016llx -> 0x%016llx %s %s\n", regions[i].begin, regions[i].end, prots[regions[i].perms], tag);
    }
    uc_free(regions);
}

static bool cb_invalid_rw(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, struct emulator_ctx *ctx) {
    printf("cb_invalid_rw %s %p\n", uc_mem_type_to_string(type), (void*)address);
    if (address < ctx->pagezero_size) {
        // in page zero
        return false;
    }
    
    // might be ok, find and map
    return mem_map_region_containing(uc, address, UC_PROT_READ | UC_PROT_WRITE);
}

static bool cb_invalid_fetch(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, struct emulator_ctx *ctx) {
    printf("cb_invalid_fetch %s %p\n", uc_mem_type_to_string(type), (void*)address);
    if (address < ctx->pagezero_size) {
        // in page zero
        return false;
    } else if (address == ctx->return_ptr) {
        // emulation done
        uc_emu_stop(uc);
        return true;
    } else {
        // call to native? caught in run_emulator
        return false;
    }
}

static bool cb_print_disasm(uc_engine *uc, uint64_t address, uint32_t size, struct emulator_ctx *ctx) {
    size_t cs_size = 4;
    uint8_t cs_code[4];
    const uint8_t *code_ptr = cs_code;
    uint64_t cs_addr = address;
    maybe_print_regs(uc, 1);
    uc_err err = uc_mem_read(uc, cs_addr, cs_code, cs_size);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "Can't even read memory to disassemble at %p: %s\n", (void*)address, uc_strerror(err));
        abort();
    }
    if (cs_disasm_iter(ctx->capstone, &code_ptr, &cs_size, &cs_addr, &ctx->insn)) {
        printf("0x%" PRIx64 ":\t%-12s%s\n", ctx->insn.address, ctx->insn.mnemonic, ctx->insn.op_str);
    } else {
        printf("0x%" PRIx64 ":\t%-12s0x%" PRIx32 "\n", (uint64_t)address, "dd", *(uint32_t*)address);
    }
    
    return true;
}
