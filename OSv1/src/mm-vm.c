//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct* rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;
  rg_elmt->rg_next = rg_node;
  mm->mmap->vm_freerg_list = rg_elmt;
  return 0;
}

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  struct vm_area_struct *pvma= mm->mmap;

  if(mm->mmap == NULL)
    return NULL;

  int vmait = 0;
  
  while (vmait < vmaid)
  {
    if(pvma == NULL)
	  return NULL;

    vmait++;
    pvma = pvma->vm_next;
  }

  return pvma;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if(rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size 
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, int size, uint32_t *alloc_addr)
{
  /*Allocate at the toproof */
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));
  int inc_sz = PAGING_PAGE_ALIGNSZ(size);
  if (get_free_vmrg_area(caller, vmaid, inc_sz, rgnode) == 0)
  {
    int incnumpage =  inc_sz / PAGING_PAGESZ;
    if (vm_map_ram(caller, rgnode->rg_start, rgnode->rg_end, 
                    rgnode->rg_start, incnumpage , rgnode) < 0)
      return -1;
    
    caller->mm->symrgtbl[rgid] = malloc(sizeof(struct vm_rg_struct));
    caller->mm->symrgtbl[rgid]->rg_start = rgnode->rg_start;
    caller->mm->symrgtbl[rgid]->rg_end = rgnode->rg_end;
    caller->mm->symrgtbl[rgid]->rg_next = NULL;
    *alloc_addr = rgnode->rg_start;
    
    free(rgnode);
    return 0;
  }
  free(rgnode);
  
  /*Attempt to increate limit to get space */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  
  //int inc_limit_ret
  int old_sbrk ;

  old_sbrk = cur_vma->sbrk;

  if (inc_vma_limit(caller, vmaid, inc_sz) < 0) return -1;

  /*Successful increase limit */
  caller->mm->symrgtbl[rgid] = malloc(sizeof(struct vm_rg_struct));
  caller->mm->symrgtbl[rgid]->rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid]->rg_end = old_sbrk + inc_sz;
  caller->mm->symrgtbl[rgid]->rg_next = NULL;
  *alloc_addr = old_sbrk;

  return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size 
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  struct vm_rg_struct *rgnode;

  if(rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return -1;

  /* TODO: Manage the collect freed region to freerg_list */
  rgnode = caller->mm->symrgtbl[rgid];
  caller->mm->symrgtbl[rgid] = NULL;
  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, rgnode);
  
  unsigned long rgstart = rgnode->rg_start;
  unsigned long rgend = rgnode->rg_end;
  unsigned long pgstart = PAGING_PGN(rgstart);
  unsigned long pgend = PAGING_PGN(rgend);

  // Loop through the page table entries from pgstart to pgend
  for (int i = pgstart; i < pgend; i++) {
      // Get the page table entry from the caller's memory management structure
      uint32_t pte = caller->mm->pgd[i];
      
      if (PAGING_PAGE_PRESENT(pte)) {
        // Extract the frame number from the page table entry
        int frmnum = PAGING_FPN(pte);
        MEMPHY_get_usedfp(caller->mram, frmnum, RAM_LCK);

        // Free the frame using the frame number
        MEMPHY_put_freefp(caller->mram, frmnum, RAM_LCK);
      } else if (GETVAL(pte, PAGING_PTE_SWAPPED_MASK, 0) != 0) {
        int frmnum = PAGING_SWP(pte);
        MEMPHY_put_freefp(caller->active_mswp, frmnum, SWP_LCK);
      } else {
#ifdef DEBUG
        printf("Freed invalid page: %d\n", i);
#endif
        continue;
      }
      
      // Clear the page table entry
      caller->mm->pgd[i] = 0;   
      if (tlb_cache_read(caller->tlb, caller->pid, i, &pte) == 0)
        tlb_cache_write(caller->tlb, caller->pid, i, 0);
  }

  return 0;
}

/*pgalloc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size 
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int pgalloc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
  addr_t addr;

  /* By default using vmaid = 0 */
  return __alloc(proc, proc->mm->mmap->vm_id, reg_index, size, &addr);
}

/*pgfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size 
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int pgfree_data(struct pcb_t *proc, uint32_t reg_index)
{
   return __free(proc, proc->mm->mmap->vm_id, reg_index);
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  if (pgn >= PAGING_MAX_PGN) return -1;
  uint32_t pte = mm->pgd[pgn];
 
  if (!PAGING_PAGE_PRESENT(pte))
  { 
    /* Page is not online, make it actively living */
    int vicpgn, swpfpn, vicfpn; 
    struct mm_struct *vicmm;
    int tgtfpn = PAGING_SWP(pte);//the target frame storing our variable
    /* TODO: Play with your paging theory here */
    /* Find victim page */
    if (MEMPHY_pop_usedfp(caller->mram, &vicfpn, &vicpgn, &vicmm, RAM_LCK) < 0)
      return -1;
    /* Get free frame in MEMSWP */
    if (MEMPHY_get_freefp(caller->active_mswp, &swpfpn, SWP_LCK) < 0) 
      return -1;

    /* Do swap frame from MEMRAM to MEMSWP and vice versa*/
    /* Copy victim frame to swap */
    __swap_cp_page(vicmm->owner->mram, vicfpn, vicmm->owner->active_mswp, swpfpn, SWP_LCK);
    /* Copy target frame from swap to mem */
    __swap_cp_page(caller->active_mswp, tgtfpn, caller->mram, vicfpn, RAM_LCK);
    MEMPHY_put_freefp(caller->active_mswp, tgtfpn, SWP_LCK);
    /* Update page table */
    pte_set_swap(&vicmm->pgd[vicpgn], 0, swpfpn);
    /* Update its online status of the target page */
    pte_set_fpn(&mm->pgd[pgn], vicfpn);

#ifdef CPU_TLB
    /* Update its online status of TLB */
    uint32_t tmppte;
    if (tlb_cache_read(vicmm->owner->tlb, vicmm->owner->pid, vicpgn, &tmppte) == 0)
      tlb_cache_write(vicmm->owner->tlb, vicmm->owner->pid, vicpgn, vicmm->pgd[vicpgn]);
#endif
    MEMPHY_put_usedfp(caller->mram, vicfpn, mm, pgn, RAM_LCK);
  } else {
    int tgtfpn = PAGING_FPN(pte);
    MEMPHY_get_usedfp(caller->mram, tgtfpn, RAM_LCK);
    MEMPHY_put_usedfp(caller->mram, tgtfpn, mm, pgn, RAM_LCK);
  }
  tlb_cache_write(caller->tlb, caller->pid, pgn, mm->pgd[pgn]);
  *fpn = PAGING_FPN(mm->pgd[pgn]);

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess 
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if(pg_getpage(mm, pgn, &fpn, caller) != 0) 
    return -1; /* invalid page access */

  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  MEMPHY_read(caller->mram, phyaddr, data);
  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess 
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if(pg_getpage(mm, pgn, &fpn, caller) != 0) 
    return -1; /* invalid page access */
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
  
  MEMPHY_write(caller->mram, phyaddr, value, RAM_LCK);
  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region 
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size 
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE *data)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if(currg == NULL || cur_vma == NULL) /* Invalid memory identify */
	  return -1;

  pg_getval(caller->mm, currg->rg_start + offset, data, caller);

  return 0;
}


/*pgwrite - PAGING-based read a region memory */
int pgread(
		struct pcb_t * proc, // Process executing the instruction
		uint32_t source, // Index of source register
		uint32_t offset, // Source address = [source] + [offset]
		uint32_t destination) 
{
  BYTE data;
  int val = __read(proc, proc->mm->mmap->vm_id, source, offset, &data);

  destination = (uint32_t) data;
#ifdef IODUMP
  printf("read region=%d offset=%d value=%d\n", source, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  //MEMPHY_dump(proc->mram);
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region 
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size 
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE value)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  
  if(currg == NULL || cur_vma == NULL) /* Invalid memory identify */
	  return -1;

  pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  return 0;
}

/*pgwrite - PAGING-based write a region memory */
int pgwrite(
		struct pcb_t * proc, // Process executing the instruction
		BYTE data, // Data to be wrttien into memory
		uint32_t destination, // Index of destination register
		uint32_t offset)
{
#ifdef IODUMP
  printf("write region=%d offset=%d value=%d\n", destination, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  //MEMPHY_dump(proc->mram);
#endif

  return __write(proc, proc->mm->mmap->vm_id, destination, offset, data);
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  int pagenum, fpn;
  uint32_t pte;


  for(pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte= caller->mm->pgd[pagenum];

    if (!PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->mram, fpn, RAM_LCK);
    } else {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->active_mswp, fpn, SWP_LCK);    
    }
  }

  return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct* get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, int size, int alignedsz)
{
  struct vm_rg_struct * newrg;
  struct vm_area_struct *cur_vma = caller->mm->mmap;

  newrg = malloc(sizeof(struct vm_rg_struct));

  newrg->rg_start = cur_vma->sbrk;

  newrg->rg_end = newrg->rg_start + size;

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, int vmastart, int vmaend)
{
  struct vm_area_struct *vma = caller->mm->mmap;

  /* TODO validate the planned memory area is not overlapped */
  if (vmastart < vma->vm_start || vmaend > vma->vm_end) {
    return -1;
  }
  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size 
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, int inc_sz)
{
  struct vm_rg_struct * newrg = malloc(sizeof(struct vm_rg_struct));
  int inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);
  int incnumpage =  inc_amt / PAGING_PAGESZ;
  struct vm_rg_struct *area = get_vm_area_node_at_brk(caller, vmaid, inc_sz, inc_amt);
  struct vm_area_struct *cur_vma = caller->mm->mmap;
  
  int old_end = cur_vma->sbrk;

  /*Validate overlap of obtained region */
  if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) < 0)
    return -1; /*Overlap and failed allocation */
  
  /* The obtained vm area (only) 
   * now will be alloc real ram region */
  cur_vma->sbrk += inc_sz;
  if (vm_map_ram(caller, area->rg_start, area->rg_end, 
                    old_end, incnumpage , newrg) < 0)
    return -1; /* Map the memory to MEMRAM */

  return 0;

}

/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, int *retpgn) 
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (pg == NULL) return -1;
  *retpgn = pg->pgn;
  mm->fifo_pgn = pg->pg_next;
  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size 
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = caller->mm->mmap;

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
  
  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;
  if (rgit->rg_start + size <= rgit->rg_end) {
    newrg->rg_start = rgit->rg_start;
    newrg->rg_end = rgit->rg_start + size;
    newrg->rg_next = NULL;
    if (rgit->rg_start + size < rgit->rg_end) {
      rgit->rg_start = rgit->rg_start + size;
    } else {
      cur_vma->vm_freerg_list = rgit->rg_next;
      free(rgit);
    }
    
    return 0;
  }
  /* Traverse on list of free vm region to find a fit space */
  while (rgit->rg_next != NULL)
  {
    struct vm_rg_struct *rg = rgit->rg_next;
    if (rg->rg_start + size <= rg->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rg->rg_start;
      newrg->rg_end = rg->rg_start + size;

      /* Update left space in chosen region */
      if (rg->rg_start + size < rg->rg_end)
      {
        rg->rg_start = rg->rg_start + size;
      }
      else
      { 
        rgit->rg_next = rg->rg_next;
        free(rg);
      }
      return 0;
    }
    else
    {
      rgit = rgit->rg_next;	// Traverse next rg
    }
  }

 if(newrg->rg_start == -1) // new region not found
   return -1;

 return 0;
}

//#endif
