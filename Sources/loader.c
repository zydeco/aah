#include "aah.h"
#include <sys/mman.h>

static void map_image(const struct mach_header_64 *mh, intptr_t vmaddr_slide);
static void setup_image_emulation(const struct mach_header_64 *mh, intptr_t vmaddr_slide);
static void load_lazy_symbols(const struct mach_header_64 *mh, intptr_t vmaddr_slide);
static void did_load_image(const struct mach_header* mh, intptr_t vmaddr_slide);

hidden void init_loader() {
    _dyld_register_func_for_add_image(did_load_image);
}

static void did_load_image(const struct mach_header* mh, intptr_t vmaddr_slide) {
    const struct mach_header_64 *mh64 = (const struct mach_header_64*)mh;
    if (should_emulate_image(mh64)) {
        setup_image_emulation(mh64, vmaddr_slide);
        load_lazy_symbols(mh64, vmaddr_slide);
    }
    map_image(mh64, vmaddr_slide);
}

static void setup_image_emulation(const struct mach_header_64 *mh, intptr_t vmaddr_slide) {
    Dl_info info;
    dladdr(mh, &info);
    printf("Setting up emulation for %s with slide 0x%lx\n", info.dli_fname, vmaddr_slide);
    
    void *lc_ptr = (void*)mh + sizeof(struct mach_header_64);
    for(uint32_t i = 0; i < mh->ncmds; i++) {
        const struct segment_command_64 *sc = lc_ptr;
        if (sc->cmd == LC_SEGMENT_64) {
            intptr_t seg_base = sc->vmaddr + vmaddr_slide;
            if (strncmp(sc->segname, SEG_TEXT, sizeof(sc->segname)) == 0) {
                // make text segment non-executable
                printf("Found text segment at 0x%lx\n", seg_base);
                if (mprotect((void*)seg_base, sc->vmsize, PROT_READ)) {
                    printf("mprotect: %s\n", strerror(errno));
                }
            }
            
            // check sections
            const struct section_64 *sect = lc_ptr + sizeof(struct segment_command_64);
            const struct section_64 *sect_end = sect + sc->nsects;
            for(; sect < sect_end; sect++) {
                uint8_t type = sect->flags & SECTION_TYPE;
                if (type == S_MOD_INIT_FUNC_POINTERS || type == S_MOD_TERM_FUNC_POINTERS) {
                    // constructors & destructors
                    void **pointers = (void **)((uintptr_t)vmaddr_slide + sect->addr);
                    for(int i = 0; i < sect->size / 8; i++) {
                        // pointers are already slid by the loader
                        cif_cache_add(pointers[i], "v");
                    }
                }
            }
        }
        lc_ptr += sc->cmdsize;
    }
}

hidden void* resolve_symbol(const char *lib_name, const char *symbol_name) {
    // find own path
    static char *full_path;
    static char *after_path;
    static uint32_t bufsize = 0;
    static unsigned long extra_size = 512;
    if (bufsize == 0) {
        _NSGetExecutablePath(NULL, &bufsize);
        bufsize += extra_size;
        full_path = malloc(bufsize);
        _NSGetExecutablePath(full_path, &bufsize);
        after_path = strrchr(full_path, '/');
    }
    
    // find path
    strlcpy(after_path, lib_name, extra_size);

    // resolve symbol
    void *handle;
    if (strncmp(lib_name, "@executable_path/", 17) == 0) {
        strncpy(after_path, lib_name+16, strlen(lib_name) - 15);
        char *real_path = realpath(full_path, NULL);
        handle = dlopen(real_path, RTLD_NOLOAD | RTLD_LAZY | RTLD_LOCAL | RTLD_FIRST);
        free(real_path);
    } else {
        handle = dlopen(lib_name, RTLD_NOLOAD | RTLD_LAZY | RTLD_LOCAL | RTLD_FIRST);
    }
    if (handle == NULL) {
        handle = RTLD_DEFAULT;
    }
    void *symbol = dlsym(handle, symbol_name);
    if (handle != RTLD_DEFAULT) {
        dlclose(handle);
    }
    return symbol;
}

static void load_lazy_symbols(const struct mach_header_64 *mh, intptr_t vmaddr_slide) {
    const struct segment_command_64 *lc_text = NULL, *lc_linkedit = NULL;
    const struct symtab_command *lc_symtab = NULL;
    const struct dysymtab_command *lc_dysymtab = NULL;
    const struct section_64 *la_symbol_section = getsectbynamefromheader_64(mh, SEG_DATA, "__la_symbol_ptr");
    const struct dylib_command *lc_dylibs[mh->ncmds];
    const struct entry_point_command *lc_main;
    memset(lc_dylibs, 0, mh->ncmds * sizeof(void*));
    
    // find own path
    uint32_t bufsize = 8192;
    char *full_path = malloc(bufsize);
    _NSGetExecutablePath(full_path, &bufsize);
    char *after_path = strrchr(full_path, '/');
    
    // find load commands
    size_t next_dylib = 0;
    void *lc_ptr = (void*)mh + sizeof(struct mach_header_64);
    for(uint32_t i = 0; i < mh->ncmds; i++) {
        const struct segment_command_64 *sc = lc_ptr;
        if (sc->cmd == LC_SEGMENT_64) {
            if (strncmp(sc->segname, SEG_TEXT, sizeof(sc->segname)) == 0) {
                lc_text = sc;
            } else if (strncmp(sc->segname, SEG_LINKEDIT, sizeof(sc->segname)) == 0) {
                lc_linkedit = sc;
            }
        } else if (sc->cmd == LC_SYMTAB) {
            lc_symtab = (const struct symtab_command*)sc;
        } else if (sc->cmd == LC_DYSYMTAB) {
            lc_dysymtab = (const struct dysymtab_command*)sc;
        } else if (sc->cmd == LC_LOAD_DYLIB || sc->cmd == LC_LOAD_WEAK_DYLIB) {
            lc_dylibs[next_dylib++] = (const struct dylib_command*)sc;
        } else if (sc->cmd == LC_MAIN) {
            lc_main = (const struct entry_point_command*)sc;
            void *pmain = (void*)(vmaddr_slide + lc_text->vmaddr + lc_main->entryoff);
            printf("main at %p\n", pmain);
            cif_cache_add(pmain, "ii???");
        }
        lc_ptr += sc->cmdsize;
    }
    
    // load lazy symbols
    if (lc_linkedit && lc_symtab && lc_dysymtab && la_symbol_section && lc_dysymtab->nindirectsyms) {
        printf("loading lazy symbols\n");
        uint64_t linkedit_base = lc_linkedit->vmaddr + vmaddr_slide - lc_linkedit->fileoff;
        const uint32_t *indirect_symtab = (const uint32_t *)(linkedit_base + lc_dysymtab->indirectsymoff);
        const struct nlist_64 *symtab = (const struct nlist_64 *)(linkedit_base + lc_symtab->symoff);
        const char *strtab = (const char *)(linkedit_base + lc_symtab->stroff);
        const uint32_t *indirect_symbol_indices = indirect_symtab + la_symbol_section->reserved1;
        void **indirect_symbol_bindings = (void **)((uintptr_t)vmaddr_slide + la_symbol_section->addr);
        
        for(size_t i = 0; i < la_symbol_section->size / 8; i++) {
            uint32_t symtab_index = indirect_symbol_indices[i];
            
            // find library name
            size_t lib_index = GET_LIBRARY_ORDINAL(symtab[symtab_index].n_desc);
            if (lib_index == BIND_SPECIAL_DYLIB_SELF) {
                continue;
            }
            const struct dylib_command *dylib = lc_dylibs[lib_index-1];
            const char *lib_name = (void*)dylib + dylib->dylib.name.offset; // it's always padded with at least one zero
            
            // resolve symbol
            uint32_t strtab_offset = symtab[symtab_index].n_un.n_strx;
            const char *symbol_name = strtab + strtab_offset;
            if (symbol_name[0] != '_') continue;
            //bool n_indr = symtab[symtab_index].n_type & N_INDR;
            void *symbol = resolve_symbol(lib_name, &symbol_name[1]);
            indirect_symbol_bindings[i] = symbol;
            
            Dl_info info;
            dladdr(symbol, &info);
            
            printf("  symbol %s (%zu: %s (%s)) -> %p\n", symbol_name, lib_index, lib_name, info.dli_fname, symbol);
            
            // fill cif cache
            cif_cache_add(symbol, lookup_method_signature(lib_name, symbol_name+1));
        }
    } else {
        printf("not loading lazy symbols\n");
    }
}

static void map_image(const struct mach_header_64 *mh, intptr_t vmaddr_slide) {
    uc_engine *uc = get_emulator_ctx()->uc;
    Dl_info info;
    dladdr((void*)mh, &info);
    //printf("Mapping image %s\n", info.dli_fname);
    
    void *lc_ptr = (void*)mh + sizeof(struct mach_header_64);
    for(uint32_t i = 0; i < mh->ncmds; i++) {
        const struct segment_command_64 *sc = lc_ptr;
        if (sc->cmd == LC_SEGMENT_64 && (strncmp(sc->segname, SEG_TEXT, sizeof(sc->segname)) == 0 || strncmp(sc->segname, SEG_DATA, sizeof(sc->segname)) == 0)) {
            uint64_t seg_addr = sc->vmaddr + (uint64_t)vmaddr_slide;
            uint64_t seg_size = (sc->vmsize + 0xfff) & 0xfffffffffffff000ULL;
            uint32_t perms = sc->maxprot & ~VM_PROT_EXECUTE;
            if ((sc->maxprot & VM_PROT_EXECUTE) && should_emulate_image(mh)) {
                // luckily, VM_PROT_* == UC_PROT_*
                perms |= UC_PROT_EXEC;
            }
            //printf("Mapping segment %s at 0x%llx (0x%llx -> 0x%llx) with perms %x\n", sc->segname, seg_addr, sc->vmsize, seg_size, perms);
            uc_err err = uc_mem_map_ptr(uc, seg_addr, seg_size, perms, (void*)seg_addr);
            if (err != UC_ERR_OK) {
                fprintf(stderr, "uc_mem_map_ptr: %u %s\n", err, uc_strerror(err));
                abort();
            }
        }
        lc_ptr += sc->cmdsize;
    }
}
