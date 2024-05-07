/*
 * Copyright (C) 2024 pdnguyen of the HCMC University of Technology
 */
/*
 * Source Code License Grant: Authors hereby grants to Licensee 
 * a personal to use and modify the Licensed Source Code for 
 * the sole purpose of studying during attending the course CO2018.
 */
//#ifdef CPU_TLB
/*
 * CPU TLB
 * TLB module cpu/cpu-tlb.c
 */
 
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>

int tlb_change_all_page_tables_of(struct pcb_t *proc,  struct memphy_struct * mp)
{
  /* TODO update all page table directory info 
   *      in flush or wipe TLB (if needed)
   */

  return 0;
}

int tlb_flush_tlb_of(struct pcb_t *proc, struct memphy_struct * mp)
{
  /* TODO flush tlb cached*/
#ifdef CPU_TLB
  for (uint32_t i = 0; i < proc->tlb->maxsz; i++) {
    proc->tlb->storage[i] = 0;
  }
#endif
  return 0;
}

/*tlballoc - CPU TLB-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size 
 *@reg_index: register index
 */
int tlballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
  addr_t addr;
  /* By default using vmaid = 0 */
  int rgid = 0;
  
  while (rgid < PAGING_MAX_SYMTBL_SZ 
        && proc->mm->symrgtbl[rgid] != NULL) 
    rgid++;
  if (rgid == PAGING_MAX_SYMTBL_SZ) {
#ifdef DEBUG
    printf("WARNING: Out of memory region\n");
#endif
    return -1;
  }
  
  int val = __alloc(proc, 0, rgid, size, &addr);

#ifdef DEBUG
  printf("Allocated %d for memory region %d:\n", size, rgid);
  print_pgtbl(proc, 0, -1);
#endif

  if (val < 0) 
    return -1;

  proc->regs[reg_index] = addr;

  return 0;
}

/*pgfree - CPU TLB-based free a region memory
 *@proc: Process executing the instruction
 *@reg_index: register index
 */
int tlbfree_data(struct pcb_t *proc, uint32_t reg_index)
{
  addr_t addr = proc->regs[reg_index];
  int rgid;
  for (rgid = 0; rgid < PAGING_MAX_SYMTBL_SZ; rgid++) {
    if (proc->mm->symrgtbl[rgid] != NULL 
        && proc->mm->symrgtbl[rgid]->rg_start == addr) 
      break;
  }
  if (rgid == PAGING_MAX_SYMTBL_SZ) 
    return -1;
  int val = __free(proc, 0, rgid);
#ifdef DEBUG
  printf("Freed region %d:\n", rgid);
  print_pgtbl(proc, 0, -1);
#endif
  return val;
}


/*tlbread - CPU TLB-based read a region memory
 *@proc: Process executing the instruction
 *@source: index of source register
 *@offset: source address = [source] + [offset]
 *@destination: destination storage
 */
int tlbread(struct pcb_t * proc, uint32_t source,
            uint32_t offset, 	uint32_t destination) 
{
  BYTE data;
	int frmnum = -1;
  
  addr_t addr = proc->regs[source];
  int pgn = PAGING_PGN((addr + offset));
	uint32_t pte;
  if (tlb_cache_read(proc->tlb, proc->pid, pgn, &pte) == 0
      && PAGING_PAGE_PRESENT(pte)) {
    frmnum = PAGING_FPN(pte);
  }

#ifdef IODUMP
  if (frmnum >= 0)
    printf("TLB hit at read source=%d offset=%d\n", 
	         source, offset);
  else 
    printf("TLB miss at read source=%d offset=%d\n", 
	         source, offset);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif  
#endif
  int val;
  if (frmnum < 0) {
    int rgid;
    for (rgid = 0; rgid < PAGING_MAX_SYMTBL_SZ; rgid++) {
      if (proc->mm->symrgtbl[rgid] != NULL 
          && proc->mm->symrgtbl[rgid]->rg_start == addr) 
        break;
    }
    if (rgid == PAGING_MAX_SYMTBL_SZ) {
#ifdef DEBUG
      printf("WARNING: No region found\n");
#endif
      return -1;
    }
    val = __read(proc, 0, rgid, offset, &data);
    if (val < 0) {
#ifdef DEBUG
      printf("WARNING: Read error\n");
#endif
      return -1;
    }
  } else {
    addr += offset;
    int off = PAGING_OFFST(addr);
    int phyaddr = (frmnum << PAGING_ADDR_FPN_LOBIT) + off;
    val = MEMPHY_read(proc->mram, phyaddr, &data);
    if (val < 0) {
#ifdef DEBUG
      printf("WARNING: Read error\n");
#endif
      return -1;
    }
  }
  if (val < 0) 
    return -1;
  proc->regs[destination] = (uint32_t) data;
#ifdef IODUMP
  printf("read data=%d\n", data);
#endif
#ifdef DEBUG
  frmnum = PAGING_FPN(proc->mm->pgd[pgn]);
  MEMPHY_dump(proc->mram, frmnum, offset, offset + 1);
#endif
  return 0;
}

/*tlbwrite - CPU TLB-based write a region memory
 *@proc: Process executing the instruction
 *@data: data to be wrttien into memory
 *@destination: index of destination register
 *@offset: destination address = [destination] + [offset]
 */
int tlbwrite(struct pcb_t * proc, BYTE data,
             uint32_t destination, uint32_t offset)
{
  int val;
  int frmnum = -1;
  
  addr_t addr = proc->regs[destination];
  int pgn = PAGING_PGN((addr + offset));
	uint32_t pte;
  if (tlb_cache_read(proc->tlb, proc->pid, pgn, &pte) == 0
      && PAGING_PAGE_PRESENT(pte)) {
    frmnum = PAGING_FPN(pte);
  }

#ifdef IODUMP
  if (frmnum >= 0)
    printf("TLB hit at write destination=%d offset=%d value=%d\n",
	          destination, offset, data);
	else
    printf("TLB miss at write destination=%d offset=%d value=%d\n",
            destination, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
#endif
  if (frmnum < 0) {
    int rgid;
    for (rgid = 0; rgid < PAGING_MAX_SYMTBL_SZ; rgid++) {
      if (proc->mm->symrgtbl[rgid] != NULL 
          && proc->mm->symrgtbl[rgid]->rg_start == addr) 
        break;
    }
    if (rgid == PAGING_MAX_SYMTBL_SZ) {
#ifdef DEBUG
      printf("WARNING: No region found\n");
#endif
      return -1;
    }
    val = __write(proc, 0, rgid, offset, data);
    if (val < 0) {
#ifdef DEBUG
      printf("WARNING: Write error\n");
#endif
      return -1;
    }
  } else {
    addr += offset;
    int off = PAGING_OFFST(addr);
    int phyaddr = (frmnum << PAGING_ADDR_FPN_LOBIT) + off;
    val = MEMPHY_write(proc->mram, phyaddr, data, RAM_LCK);
    if (val < 0) {
#ifdef DEBUG
      printf("WARNING: Write error\n");
#endif
      return -1;
    }
  }
#ifdef DEBUG
  frmnum = PAGING_FPN(proc->mm->pgd[pgn]);
  MEMPHY_dump(proc->mram, frmnum, offset, offset + 1);
#endif
  return val;
}

//#endif
