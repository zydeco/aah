#include "aah.h"
#include <sys/mman.h>

static void map_image(const struct mach_header_64 *mh, intptr_t vmaddr_slide);
static void setup_image_emulation(const struct mach_header_64 *mh, intptr_t vmaddr_slide);
static void load_lazy_symbols(const struct mach_header_64 *mh, intptr_t vmaddr_slide);
static void did_load_image(const struct mach_header* mh, intptr_t vmaddr_slide);

hidden void init_loader() {
    load_lazy_symbols((const struct mach_header_64*)_dyld_get_image_header(0), _dyld_get_image_vmaddr_slide(0));
    _dyld_register_func_for_add_image(did_load_image);
}

static void did_load_image(const struct mach_header* mh, intptr_t vmaddr_slide) {
    const struct mach_header_64 *mh64 = (const struct mach_header_64*)mh;
    if (should_emulate_image(mh64)) {
        setup_image_emulation(mh64, vmaddr_slide);
    }
    map_image(mh64, vmaddr_slide);
}

static void setup_image_emulation(const struct mach_header_64 *mh, intptr_t vmaddr_slide) {
    Dl_info info;
    dladdr(mh, &info);
    printf("Setting up emulation for %s\n", info.dli_fname);
    
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
                        cif_cache_add(vmaddr_slide + pointers[i], "v");
                    }
                }
            }
        }
        lc_ptr += sc->cmdsize;
    }
}

static void load_lazy_symbols(const struct mach_header_64 *mh, intptr_t vmaddr_slide) {
    const struct segment_command_64 *lc_text = NULL, *lc_linkedit = NULL;
    const struct symtab_command *lc_symtab = NULL;
    const struct dysymtab_command *lc_dysymtab = NULL;
    const struct section_64 *la_symbol_section = getsectbynamefromheader_64(mh, SEG_DATA, "__la_symbol_ptr");
    const struct dylib_command *lc_dylibs[mh->ncmds];
    const struct entry_point_command *lc_main;
    memset(lc_dylibs, 0, mh->ncmds * sizeof(void*));
    
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
            // resolve symbol
            uint32_t symtab_index = indirect_symbol_indices[i];
            uint32_t strtab_offset = symtab[symtab_index].n_un.n_strx;
            const char *symbol_name = strtab + strtab_offset;
            if (symbol_name[0] != '_') continue;
            // FIXME: use library instead of RTLD_DEFAULT
            void *symbol = dlsym(RTLD_DEFAULT, &symbol_name[1]);
            indirect_symbol_bindings[i] = symbol;
            
            // find library name
            size_t lib_index = GET_LIBRARY_ORDINAL(symtab[symtab_index].n_desc);
            const struct dylib_command *dylib = lc_dylibs[lib_index-1];
            const char *lib_name = (void*)dylib + dylib->dylib.name.offset; // it's always padded with at least one zero
            printf("  symbol %s (%zu: %s) -> %p\n", symbol_name, lib_index, lib_name, symbol);
            
            // fill cif cache
            cif_cache_add(symbol, lookup_method_signature(lib_name, symbol_name+1));
        }
    } else {
        printf("not loading lazy symbols\n");
    }
}

static void map_image(const struct mach_header_64 *mh, intptr_t vmaddr_slide) {
    uc_engine *uc = get_unicorn();
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
