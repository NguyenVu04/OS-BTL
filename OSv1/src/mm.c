//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm.h"
#include <stdlib.h>
#include <stdio.h>

/* 
 * init_pte - Initialize PTE entry
 */
int init_pte(uint32_t *pte,
             int pre,    // present
             int fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             int swpoff) //swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      /*if (fpn == 0) 
        return -1; */// Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT); 
    } else { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT); 
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;   
}

/* 
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(uint32_t *pte, int swptyp, int swpoff)
{
  //SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/* 
 * pte_set_swap - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(uint32_t *pte, int fpn)
{
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT); 

  return 0;
}


/* 
 * vmap_page_range - map a range of page at aligned address
 */
int vmap_page_range(struct pcb_t *caller, // process call
                                int addr, // start address which is aligned to pagesz
                               int pgnum, // num of mapping page
           struct framephy_struct *frames,// list of the mapped frames
           struct framephy_struct *swpframes,// list of the swapped frames
              struct vm_rg_struct *ret_rg)// return mapped region, the real mapped fp
{                                         // no guarantee all given pages are mapped
  /* Iterate through each page to be mapped and set up the page table entries (PTEs)
   * for the process's page directory (PGD) and update the region information. */
  struct framephy_struct *fpit = frames;  // Pointer to the first frame in the list
  int pgit = 0;  // Page index
  int pgn = PAGING_PGN(addr);  // Page number

  /* Initialize region end and start to the start address, and set the next pointer to NULL. */
  ret_rg->rg_end = ret_rg->rg_start = addr;  // First space is usable
  ret_rg->rg_next = NULL;
  
  /* Loop through each page to be mapped, updating the page table entry (PTE) for the
   * process's page directory (PGD) and updating the region information. */
  while (fpit != NULL) {
    /* Move the end of the region to the next page boundary. */
    ret_rg->rg_end += PAGING_PAGESZ;
    
    /* Initialize the page table entry (PTE) for the process's page directory (PGD). */
    init_pte(&caller->mm->pgd[pgn + pgit], 1, fpit->fpn, 0, 0, 0, 0);
    
    /* Move the frame from the list to the used frame list in the memory. */
    MEMPHY_put_usedfp(caller->mram, fpit->fpn, caller->mm, pgn + pgit, RAM_LCK);
    
    /* Update the TLB with the new PTE. */
    tlb_cache_write(caller->tlb, caller->pid, pgn + pgit, caller->mm->pgd[pgn + pgit]);
    
    /* Free the frame structure from the list and move to the next. */
    struct framephy_struct *node = fpit;
    fpit = fpit->fp_next;
    free(node);
    
    /* Move to the next page. */
    pgit++;
  }
  
  fpit = swpframes;
  while (fpit != NULL) {
    /* Move the end of the region to the next page boundary. */
    ret_rg->rg_end += PAGING_PAGESZ;
    /* Initialize the page table entry (PTE) for the process's page directory (PGD). */
    pte_set_swap(&caller->mm->pgd[pgn + pgit], 0, fpit->fpn);
    struct framephy_struct *node = fpit;
    fpit = fpit->fp_next;
    free(node);
    
    /* Move to the next page. */
    pgit++;
  }
  /* Return 0, indicating success. */
  return 0;
}

/* 
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

int alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct** frm_lst, struct framephy_struct** swp_lst)
{
// Declare variables for page index (pgit), frame number (fpn), and a pointer to a structure (newfp_str)
int pgit, fpn;
struct framephy_struct *newfp_str;

int ram_frm_num = caller->mram->maxsz / PAGING_PAGESZ;
int swp_frm_num = caller->active_mswp->maxsz / PAGING_PAGESZ;

if (req_pgnum > ram_frm_num + swp_frm_num)
  return -3000;

// Loop through each page index up to req_pgnum
for(pgit = 0; pgit < req_pgnum; pgit++)
{
    // Check if a free physical frame is available
    if(MEMPHY_get_freefp(caller->mram, &fpn, RAM_LCK) == 0)
    {
        // Allocate memory for a new frame physical structure
        newfp_str = malloc(sizeof(struct framephy_struct));
        // Assign the frame number to the new structure
        newfp_str->fpn = fpn;
        // Link the new structure to the beginning of the frame list
        newfp_str->fp_next = *frm_lst;
        // Update the frame list pointer to point to the new structure
        *frm_lst = newfp_str;
    } 
    else 
    {  
        // Declare a variable for the swapped frame number
        int swpfpn;
        struct mm_struct *vicmm;
        int pgn;
        if (MEMPHY_pop_usedfp(caller->mram, &fpn, &pgn, &vicmm, RAM_LCK) < 0) {
          // Try to get a free frame from the swap space of the caller's active memory
          if (MEMPHY_get_freefp(caller->active_mswp, &swpfpn, SWP_LCK) < 0) 
              return -3000;
          
          newfp_str = malloc(sizeof(struct framephy_struct));
          newfp_str->fpn = swpfpn;
          newfp_str->fp_next = *swp_lst;
          *swp_lst = newfp_str;
          continue;
        }
          
        // Try to get a free frame from the swap space of the caller's active memory
        if (MEMPHY_get_freefp(caller->active_mswp, &swpfpn, SWP_LCK) < 0) 
            return -3000; // Return error code if unable to get a free frame from swap
        // Swap the contents of the main memory frame with the swap frame
        __swap_cp_page(caller->mram, fpn, caller->active_mswp, swpfpn, SWP_LCK);
        // Update the page table entry of the victim process to point to the swap frame
        pte_set_swap(&vicmm->pgd[pgn], 0, swpfpn);
        // Update the TLB cache with the swapped page
        tlb_cache_write(caller->tlb, caller->pid, pgn, vicmm->pgd[pgn]);        

        // Allocate memory for a new frame physical structure
        newfp_str = malloc(sizeof(struct framephy_struct));
        // Assign the frame number to the new structure
        newfp_str->fpn = fpn;
        // Link the new structure to the beginning of the frame list
        newfp_str->fp_next = *frm_lst;
        // Update the frame list pointer to point to the new structure
        *frm_lst = newfp_str;
    } 
}

  return 0;
}


/* 
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
int vm_map_ram(struct pcb_t *caller, int astart, int aend, int mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  struct framephy_struct *swp_lst = NULL;
  int ret_alloc;
  
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst, &swp_lst);
  
  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  /* Out of memory */
  if (ret_alloc == -3000) 
  {
#ifdef MMDBG
     printf("OOM: vm_map_ram out of memory \n");
#endif
     return -1;
  }
  
  
  vmap_page_range(caller, mapstart, incpgnum, frm_lst, swp_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame 
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, int srcfpn,
                struct memphy_struct *mpdst, int dstfpn, BYTE dstoption) 
{
  int cellidx;
  int addrsrc,addrdst;
  for(cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data, dstoption);
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct * vma = malloc(sizeof(struct vm_area_struct));

  mm->pgd = malloc(PAGING_MAX_PGN*sizeof(uint32_t));
  mm->owner = caller;
  /* By default the owner comes with at least one vma */
  vma->vm_id = 0;
  vma->vm_start = 0;
  vma->vm_end = vma->vm_start + PAGING_MAX_PGN * PAGING_PAGESZ;
  vma->sbrk = vma->vm_start;
  vma->vm_freerg_list = NULL;

  vma->vm_next = NULL;
  vma->vm_mm = mm; /*point back to vma owner */

  mm->mmap = vma;
  for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
    mm->symrgtbl[i] = NULL;
  }

  mm->fifo_pgn = NULL;
  return 0;
}

struct vm_rg_struct* init_vm_rg(int rg_start, int rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct* rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct mm_struct *mm, int pgn)
{
  struct pgn_t* pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = mm->fifo_pgn;

  mm->fifo_pgn = pnode;
  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
   struct framephy_struct *fp = ifp;
 
   printf("print_list_fp: ");
   if (fp == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (fp != NULL )
   {
       printf("fp[%d]\n",fp->fpn);
       fp = fp->fp_next;
   }
   printf("\n");
   return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
   struct vm_rg_struct *rg = irg;
 
   printf("print_list_rg: ");
   if (rg == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (rg != NULL)
   {
       printf("rg[%ld->%ld]\n",rg->rg_start, rg->rg_end);
       rg = rg->rg_next;
   }
   printf("\n");
   return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
   struct vm_area_struct *vma = ivma;
 
   printf("print_list_vma: ");
   if (vma == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (vma != NULL )
   {
       printf("va[%ld->%ld]\n",vma->vm_start, vma->vm_end);
       vma = vma->vm_next;
   }
   printf("\n");
   return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
   printf("print_list_pgn: ");
   if (ip == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (ip != NULL )
   {
       printf("va[%d]-\n",ip->pgn);
       ip = ip->pg_next;
   }
   printf("n");
   return 0;
}

int print_pgtbl(struct pcb_t *caller, uint32_t start, uint32_t end)
{
  int pgn_start,pgn_end;
  int pgit;

  if(end == -1){
    struct vm_area_struct *cur_vma = caller->mm->mmap;
    end = cur_vma->sbrk;
  }
  pgn_start = PAGING_PGN(start);
  pgn_end = PAGING_PGN(end);

  printf("print_pgtbl: %d - %d", start, end);
  if (caller == NULL) {printf("NULL caller\n"); return -1;}
  printf("\n");

  for(pgit = pgn_start; pgit < pgn_end; pgit++)
  {
     printf("%08ld: %08x\n", pgit * sizeof(uint32_t), caller->mm->pgd[pgit]);
  }

  return 0;
}

//#endif
