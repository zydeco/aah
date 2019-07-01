#include "aah.h"

void mem_print_uc_regions(uc_engine *uc) {
    if (uc == NULL) {
        uc = get_unicorn();
    }
    uc_mem_region *regions;
    uint32_t num_regions;
    const char *perm_str[] = {
        [0] = "none",
        [1] = "r--",
        [2] = "-w-",
        [3] = "rw-",
        [4] = "--x",
        [5] = "r-x",
        [6] = "-wx",
        [7] = "rwx"
    };
    uc_mem_regions(uc, &regions, &num_regions);
    qsort_b(regions, num_regions, sizeof(uc_mem_region), ^int(const void * rpa, const void * rpb) {
        const uc_mem_region *ra = (const uc_mem_region*)rpa;
        const uc_mem_region *rb = (const uc_mem_region*)rpb;
        return (int)ra->begin - (int)rb->end;
    });
    printf("%d regions:\n", num_regions);
    for(uint32_t i = 0; i < num_regions; i++) {
        printf("  %p->%p %s\n", (void*)regions[i].begin, (void*)regions[i].end, perm_str[regions[i].perms]);
    }
    free(regions);
}

bool mem_map_region_containing(uc_engine *uc, uint64_t address, uint32_t perms) {
    vm_address_t region_address = (vm_address_t)address;
    vm_size_t region_size;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    memory_object_name_t object;
    kern_return_t err = vm_region_64(mach_task_self(), &region_address, &region_size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object);
    if (err != KERN_SUCCESS) {
        return false;
    }
    
    uc_err uerr = uc_mem_map_ptr(uc, region_address, region_size, perms, (void*)region_address);
    if (uerr != UC_ERR_OK) {
        printf("uc_mem_map_ptr: %s\n", uc_strerror(uerr));
        mem_print_uc_regions(uc);
        return false;
    }
    
    return true;
}

hidden void print_region_info(void *ptr) {
    vm_address_t address = (vm_address_t)ptr;
    vm_size_t size;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    memory_object_name_t object;
    kern_return_t err = vm_region_64(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object);
    printf("region info for %p:\n", ptr);
    printf("  address = %p -> %p\n", (void*)address, (void*)(address+size));
    printf("  size = 0x%lx\n", size);
    printf("  offset = 0x%llx\n", info.offset);
    printf("  protection = (%x to %x)\n", info.protection, info.max_protection);
}
