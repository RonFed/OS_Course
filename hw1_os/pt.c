#include "os.h"
#include <stdio.h>

#define PT_SIZE_BYTES                   (4096)
#define PT_ENTRY_SIZE_BYTES             (8)
#define PT_ENTRIES_NUM                  (PT_SIZE_BYTES / PT_ENTRY_SIZE_BYTES)
#define OFFSET_BITS                     (12)
#define PHYSICAL_ADDR(ppn, offset)      (ppn << OFFSET_BITS) | (offset)   

#define VPN_SUB_BIT_SIZE                (9)
#define VPN_BIT_SIZE                    (45)
#define TRIE_DEPTH                      (VPN_BIT_SIZE / VPN_SUB_BIT_SIZE)
#define VPN_BASE_MASK                   (0x1FF)
#define VPN_I(vpn, i)                   (vpn >> (VPN_SUB_BIT_SIZE*(TRIE_DEPTH - i)) & (VPN_BASE_MASK))

#define IS_VALID_PTE(pte)               (pte & 1)
#define SET_VALID(pte_ptr)              ((*pte_ptr) |= 1)
#define GET_FRAME_N(pte)                (pte >> 12)
#define SET_FRAME_N(pte_ptr, frame_n)   ((*pte_ptr) |= (frame_n << 12))

typedef uint64_t pt_entry;

typedef struct {
    pt_entry entries[PT_ENTRIES_NUM] ;
} pt_node_t;

pt_node_t* page_walk(pt_node_t* base_pt, uint64_t vpn, uint8_t allocate) {
    pt_node_t* curr_pt = base_pt;
    uint64_t vpn_i = 0;
    pt_entry* curr_entry;

    for (uint32_t i = 1; i < TRIE_DEPTH; i++) {
        /* sub vpn for this level */
        vpn_i = VPN_I(vpn, i);
        curr_entry = &curr_pt->entries[vpn_i];
        if (!IS_VALID_PTE(*curr_entry)) {
            if (!allocate) {
                /* Invalid entry and we were asked to not allocate so nothing to be done*/
                return (pt_node_t*)NO_MAPPING;
            }
            /* Allocate new page table */
            SET_FRAME_N(curr_entry, alloc_page_frame());
            SET_VALID(curr_entry);
        }
        /* Valid entry so Move to newxt page table node */
        curr_pt = phys_to_virt(PHYSICAL_ADDR(GET_FRAME_N(*curr_entry), 0));
    }

    return curr_pt;
}

void page_table_update(uint64_t pt, uint64_t vpn, uint64_t ppn) {
    pt_node_t* base_pt = phys_to_virt(PHYSICAL_ADDR(pt, 0));
    uint8_t allocate = (ppn != NO_MAPPING);

    pt_node_t* target_pt = page_walk(base_pt, vpn, allocate);

    if ((uint64_t)target_pt != NO_MAPPING) {
        pt_entry* target_entry = &target_pt->entries[VPN_I(vpn, TRIE_DEPTH)];
        if (!allocate) {
            *target_entry = 0;
        } else {
            SET_VALID(target_entry);
            SET_FRAME_N(target_entry, ppn);
        }
    }
}

uint64_t page_table_query(uint64_t pt, uint64_t vpn) {
    pt_node_t* base_pt = phys_to_virt(PHYSICAL_ADDR(pt, 0));
    pt_node_t* target_pt = page_walk(base_pt, vpn, 0);
    pt_entry target_entry;

    if ((uint64_t)target_pt == NO_MAPPING) {
        return NO_MAPPING;
    }
    
    target_entry = target_pt->entries[VPN_I(vpn, TRIE_DEPTH)];
    if (!IS_VALID_PTE(target_entry)) {
        return NO_MAPPING;
    }

    return GET_FRAME_N(target_entry);
    
}

