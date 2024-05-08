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

/**
 * tlballoc - CPU TLB-based allocates memory for a region
 * @proc: Process executing the instruction
 * @size: Size of memory to allocate
 * @reg_index: Index of register to store memory address
 *
 * Allocates memory for a region and stores the memory address in the
 * specified register.
 *
 * Returns 0 on success, -1 on failure.
 */
int tlballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
  /* The address of the allocated memory */
  addr_t addr;
  
  /* By default, use vmaid = 0 */
  int rgid = 0;
  
  /* Find the first available region id */
  while (rgid < PAGING_MAX_SYMTBL_SZ 
         && proc->mm->symrgtbl[rgid] != NULL) 
    rgid++;
  
  /* If no region is available, print warning and return -1 */
  if (rgid == PAGING_MAX_SYMTBL_SZ) {
#ifdef DEBUG
    printf("WARNING: Out of memory region\n");
#endif
    return -1;
  }
  
  /* Allocate memory for the region */
  int val = __alloc(proc, 0, rgid, size, &addr);

#ifdef DEBUG
  /* Print the resulting page table if DEBUG is defined */
  printf("Allocated %d for memory region %d:\n", size, rgid);
  print_pgtbl(proc, 0, -1);
#endif

  /* If __alloc() fails, return -1 */
  if (val < 0) 
    return -1;

  /* Store the memory address in the specified register */
  proc->regs[reg_index] = addr;

  /* Return 0 to indicate success */
  return 0;
}

/**
 * tlbfree_data - CPU TLB-based free a region memory
 * @proc: Process executing the instruction
 * @reg_index: Index of register containing address of memory to free
 *
 * Finds the region that the given address belongs to and frees the memory
 * using __free(). Prints the resulting page table using print_pgtbl() if
 * DEBUG is defined. Returns the value returned by __free().
 *
 * Returns:
 *     0 on success
 *    -1 on failure
 */
int tlbfree_data(struct pcb_t *proc, uint32_t reg_index)
{
  // Get the address from the register
  addr_t addr = proc->regs[reg_index];
  // Initialize rgid to the first region
  int rgid;
  // Find the region that the address belongs to
  for (rgid = 0; rgid < PAGING_MAX_SYMTBL_SZ; rgid++) {
    if (proc->mm->symrgtbl[rgid] != NULL 
        && proc->mm->symrgtbl[rgid]->rg_start == addr) 
      break;
  }
  // If no region was found, return -1
  if (rgid == PAGING_MAX_SYMTBL_SZ) 
    return -1;
  // Free the memory in the region
  int val = __free(proc, 0, rgid);
  // Print the resulting page table if DEBUG is defined
#ifdef DEBUG
  printf("Freed region %d:\n", rgid);
  print_pgtbl(proc, 0, -1);
#endif
  // Return the value returned by __free()
  return val;
}

/**
 * tlbread - CPU TLB-based read a region memory
 * @proc: Process executing the instruction
 * @source: Index of source register
 * @offset: Offset of memory address
 * @destination: Index of destination register
 *
 * Reads a byte of data from memory using TLB and returns it in
 * the destination register of the process.
 *
 * Returns 0 on success, -1 on failure.
 */
int tlbread(struct pcb_t * proc, uint32_t source, uint32_t offset, uint32_t destination) {
  BYTE data; // Data read from memory
  int frmnum = -1; // Frame number of page table directory
  
  addr_t addr = proc->regs[source]; // Memory address
  int pgn = PAGING_PGN((addr + offset)); // Page number
  uint32_t pte; // Page table entry
  
  /* Try to read page table entry from TLB cache */
  if (tlb_cache_read(proc->tlb, proc->pid, pgn, &pte) == 0
      && PAGING_PAGE_PRESENT(pte)) { // If entry found and page is present
    frmnum = PAGING_FPN(pte); // Get frame number of page table directory
  }

#ifdef IODUMP
  /* Print TLB hit or miss */
  if (frmnum >= 0) {
    printf("TLB hit at read source=%d offset=%d\n", source, offset);
  } else {
    printf("TLB miss at read source=%d offset=%d\n", source, offset);
  }
#ifdef PAGETBL_DUMP
  /* Print maximum page table */
  print_pgtbl(proc, 0, -1);
#endif  
#endif
#ifdef TLBDUMP
  /* Dump TLB memory physical address */
  TLBMEMPHY_dump(proc->tlb, proc->pid, pgn);
#endif
  int val; // Variable to store return value
  if (frmnum < 0) {
    int rgid; // Region ID
    /* Find region start address */
    for (rgid = 0; rgid < PAGING_MAX_SYMTBL_SZ; rgid++) {
      if (proc->mm->symrgtbl[rgid] != NULL 
          && proc->mm->symrgtbl[rgid]->rg_start == addr) {
        break;
      }
    }
    if (rgid == PAGING_MAX_SYMTBL_SZ) {
#ifdef DEBUG
      printf("WARNING: No region found\n");
#endif
      return -1;
    }
    /* Read data from region */
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
    /* Read data from memory */
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
  /* Store read data in destination register */
  proc->regs[destination] = (uint32_t) data;
#ifdef IODUMP
  printf("read data=%d\n", data);
#endif
#ifdef DEBUG
  /* Dump memory physical address */
  frmnum = PAGING_FPN(proc->mm->pgd[pgn]);
  MEMPHY_dump(proc->mram, frmnum, offset, offset + 1);
#endif
  return 0;
}

/**
 * Writes data to memory using the TLB (Translation Lookaside Buffer)
 *
 * @proc: Process control block
 * @data: Data to write to memory
 * @destination: Destination register where memory address is stored
 * @offset: Offset from the memory address in the destination register
 * @return 0 on success, -1 on failure
 */
int tlbwrite(struct pcb_t * proc, BYTE data,
             uint32_t destination, uint32_t offset)
{
  int val; // Variable to store return value

  /* Check if page table entry is in TLB cache */
  int frmnum = -1; // Frame number of page table directory
  addr_t addr = proc->regs[destination]; // Memory address
  int pgn = PAGING_PGN((addr + offset)); // Page number
  uint32_t pte; // Page table entry
  if (tlb_cache_read(proc->tlb, proc->pid, pgn, &pte) == 0
      && PAGING_PAGE_PRESENT(pte)) {
    frmnum = PAGING_FPN(pte); // Get frame number of page table directory
  }

#ifdef IODUMP
  /* Print TLB hit or miss */
  if (frmnum >= 0) {
    printf("TLB hit at write destination=%d offset=%d value=%d\n",
            destination, offset, data);
  } else {
    printf("TLB miss at write destination=%d offset=%d value=%d\n",
            destination, offset, data);
  }
#ifdef PAGETBL_DUMP
  /* Print maximum page table */
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
#endif
#ifdef TLBDUMP
  /* Dump TLB memory physical address */
  TLBMEMPHY_dump(proc->tlb, proc->pid, pgn);
#endif

  /* Write data to memory */
  if (frmnum < 0) {
    int rgid; // Region ID

    /* Find region start address */
    for (rgid = 0; rgid < PAGING_MAX_SYMTBL_SZ; rgid++) {
      if (proc->mm->symrgtbl[rgid] != NULL 
          && proc->mm->symrgtbl[rgid]->rg_start == addr) 
        break;
    }

    /* Check if region was found */
    if (rgid == PAGING_MAX_SYMTBL_SZ) {
#ifdef DEBUG
      printf("WARNING: No region found\n");
#endif
      return -1;
    }

    /* Write data to region */
    val = __write(proc, 0, rgid, offset, data);
    if (val < 0) {
#ifdef DEBUG
      printf("WARNING: Write error\n");
#endif
      return -1;
    }
  } else {
    addr += offset;
    int off = PAGING_OFFST(addr); // Offset in the page
    int phyaddr = (frmnum << PAGING_ADDR_FPN_LOBIT) + off; // Physical address
    val = MEMPHY_write(proc->mram, phyaddr, data, RAM_LCK); // Write data
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
