#include "aah.h"
#include <pthread.h>

static bool cb_invalid_rw(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, struct emulator_ctx *ctx);
static bool cb_invalid_fetch(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, struct emulator_ctx *ctx);
static bool cb_print_disasm(uc_engine *uc, uint64_t address, uint32_t size, struct emulator_ctx *ctx);
static void destroy_emulator_ctx(void *ptr);

static pthread_key_t emulator_ctx_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

static void dont_print_regs(uc_engine *uc) {};
static void(*maybe_print_regs)(uc_engine*) = dont_print_regs;

static void print_regs(uc_engine *uc) {
    struct arm64_call_context call_context;
    uint64_t pc, sp, lr, fp;
    uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
    uc_reg_read(uc, UC_ARM64_REG_SP, &sp);
    uc_reg_read(uc, UC_ARM64_REG_FP, &fp);
    uc_reg_read(uc, UC_ARM64_REG_LR, &lr);
    uc_reg_read(uc, UC_ARM64_REG_X0, &call_context.x[0]);
    uc_reg_read(uc, UC_ARM64_REG_X1, &call_context.x[1]);
    uc_reg_read(uc, UC_ARM64_REG_X2, &call_context.x[2]);
    uc_reg_read(uc, UC_ARM64_REG_X3, &call_context.x[3]);
    uc_reg_read(uc, UC_ARM64_REG_X4, &call_context.x[4]);
    uc_reg_read(uc, UC_ARM64_REG_X5, &call_context.x[5]);
    uc_reg_read(uc, UC_ARM64_REG_X6, &call_context.x[6]);
    uc_reg_read(uc, UC_ARM64_REG_X7, &call_context.x[7]);
    uc_reg_read(uc, UC_ARM64_REG_X8, &call_context.x[8]);
    uc_reg_read(uc, UC_ARM64_REG_V0, &call_context.v[0]);
    uc_reg_read(uc, UC_ARM64_REG_V1, &call_context.v[1]);
    uc_reg_read(uc, UC_ARM64_REG_V2, &call_context.v[2]);
    uc_reg_read(uc, UC_ARM64_REG_V3, &call_context.v[3]);
    uc_reg_read(uc, UC_ARM64_REG_V4, &call_context.v[4]);
    uc_reg_read(uc, UC_ARM64_REG_V5, &call_context.v[5]);
    uc_reg_read(uc, UC_ARM64_REG_V6, &call_context.v[6]);
    uc_reg_read(uc, UC_ARM64_REG_V7, &call_context.v[7]);
    
    printf("x0:0x%016llx ", call_context.x[0]);
    printf("x1:0x%016llx ", call_context.x[1]);
    printf("x2:0x%016llx ", call_context.x[2]);
    printf("x3:0x%016llx\n", call_context.x[3]);
    printf("x4:0x%016llx ", call_context.x[4]);
    printf("x5:0x%016llx ", call_context.x[5]);
    printf("x6:0x%016llx ", call_context.x[6]);
    printf("x7:0x%016llx\n", call_context.x[7]);
    printf("pc:0x%016llx ", pc);
    printf("sp:0x%016llx ", sp);
    printf("fp:0x%016llx ", fp);
    printf("lr:0x%016llx\n", lr);
}

hidden struct emulator_ctx* init_emulator_ctx() {
    struct emulator_ctx *ctx = malloc(sizeof(struct emulator_ctx));
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
    err = uc_hook_add(ctx->uc, &mem_hook, UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED, cb_invalid_rw, ctx, 8, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "uc_hook_add: %u %s\n", err, uc_strerror(err));
        abort();
    }
    
    // catch invalid execute
    uc_hook exe_hook;
    err = uc_hook_add(ctx->uc, &exe_hook, UC_HOOK_MEM_FETCH_PROT | UC_HOOK_MEM_FETCH_UNMAPPED, cb_invalid_fetch, ctx, 8, 0);
    if (err != UC_ERR_OK) {
        fprintf(stderr, "uc_hook_add: %u %s\n", err, uc_strerror(err));
        abort();
    }
    
    if (getenv("PRINT_DISASSEMBLY") && strtol(getenv("PRINT_DISASSEMBLY"), NULL, 10)) {
        // print emulated instructions as they are fetched
        uc_hook fetch_hook;
        cs_err cerr = cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &(ctx->capstone));
        if (cerr != CS_ERR_OK) {
            fprintf(stderr, "cs_open: %s\n", cs_strerror(cerr));
            abort();
        }
        err = uc_hook_add(ctx->uc, &fetch_hook, UC_HOOK_CODE, cb_print_disasm, ctx, 8, 0);
        if (err != UC_ERR_OK) {
            fprintf(stderr, "uc_hook_add: %u %s\n", err, uc_strerror(err));
            abort();
        }
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
    struct emulator_ctx *ctx = pthread_getspecific(emulator_ctx_key);
    if (ctx == NULL) ctx = init_emulator_ctx();
    return ctx->uc;
}

hidden struct emulator_ctx* get_emulator_ctx() {
    return pthread_getspecific(emulator_ctx_key);
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
    maybe_print_regs(ctx->uc);
    
    uc_engine *uc = ctx->uc;
    uint64_t pc;
    uc_err err;
    Dl_info info;
    uc_reg_write(ctx->uc, UC_ARM64_REG_LR, &ctx->return_ptr);
    for(;;) {
        err = uc_emu_start(uc, start_address, ctx->return_ptr, 0, 0);
        uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
        if (err == UC_ERR_FETCH_PROT && dladdr((void*)pc, &info) && info.dli_saddr == (void*)pc) {
            uint64_t last_lr;
            uc_reg_read(uc, UC_ARM64_REG_LR, &last_lr);
            printf("  calling native %s from %p\n", info.dli_sname, (void*)last_lr);
            call_native(uc, pc);
            // return to emulator
            start_address = last_lr;
            continue;
        } else if (pc == ctx->return_ptr) {
            printf("emulation finished ok?\n");
            return;
        } else {
            printf("emulation finished badly: %s\n", uc_strerror(err));
            exit(1);
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
    const uint8_t *cs_code = (const uint8_t *)address;
    size_t cs_size = 4;
    uint64_t cs_addr = address;
    maybe_print_regs(uc);
    if (cs_disasm_iter(ctx->capstone, &cs_code, &cs_size, &cs_addr, &ctx->insn)) {
        printf("0x%" PRIx64 ":\t%-12s%s\n", ctx->insn.address, ctx->insn.mnemonic, ctx->insn.op_str);
    } else {
        printf("0x%" PRIx64 ":\t%-12s0x%" PRIx32 "\n", (uint64_t)address, "dd", *(uint32_t*)address);
    }
    
    return true;
}
