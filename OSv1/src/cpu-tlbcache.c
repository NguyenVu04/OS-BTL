/*
 * Copyright (C) 2024 pdnguyen of the HCMC University of Technology
 */
/*
 * Source Code License Grant: Authors hereby grants to Licensee 
 * a personal to use and modify the Licensed Source Code for 
 * the sole purpose of studying during attending the course CO2018.
 */
//#ifdef MM_TLB
/*
 * Memory physical based TLB Cache
 * TLB cache module tlb/tlbcache.c
 *
 * TLB cache is physically memory phy
 * supports random access 
 * and runs at high speed
 */


#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#define init_tlbcache(mp,sz,...) init_memphy(mp, sz, (1, ##__VA_ARGS__))

pthread_mutex_t tlb_lock;
/*
 *  tlb_cache_read read TLB cache device
 *  @mp: memphy struct
 *  @pid: process id
 *  @pgnum: page number
 *  @value: obtained value
 */
int tlb_cache_read(struct memphy_struct * mp, int pid, int pgnum, uint32_t *value)
{
   /* TODO: the identify info is mapped to 
    *      cache line by employing:
    *      direct mapped, associated mapping etc.
    */
   uint32_t* storage = (uint32_t*) mp->storage;
   uint32_t i = TLB_INDEX(pid, pgnum);
   uint32_t id = (i % (mp->maxsz / 8)) * 2;
   uint32_t tag = i / (mp->maxsz / 8);
   if (storage[id] != tag) 
      return -1;
   *value = storage[id + 1];
#ifdef DEBUG
   printf("i pgn pid: %08x %d %d %d\n", i, pgnum, pid, PAGING_MAX_PGN);
#endif
   return 0;
}

/*
 *  tlb_cache_write write TLB cache device
 *  @mp: memphy struct
 *  @pid: process id
 *  @pgnum: page number
 *  @value: obtained value
 */
int tlb_cache_write(struct memphy_struct *mp, int pid, int pgnum, uint32_t value)
{
   /* TODO: the identify info is mapped to 
    *      cache line by employing:
    *      direct mapped, associated mapping etc.
    */
   uint32_t* storage = (uint32_t*) mp->storage;
   uint32_t i = TLB_INDEX(pid, pgnum);
   uint32_t id = (i % (mp->maxsz / 8)) * 2;
   uint32_t tag = i / (mp->maxsz / 8);
   pthread_mutex_lock(&tlb_lock);
   storage[id] = tag;
   storage[id + 1] = value;
   pthread_mutex_unlock(&tlb_lock);
#ifdef DEBUG
   printf("i pgn pid: %08x %d %d %d\n", i, pgnum, pid, PAGING_MAX_PGN);
#endif
   return 0;
}

/*
 *  TLBMEMPHY_read natively supports MEMPHY device interfaces
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int TLBMEMPHY_read(struct memphy_struct * mp, int addr, BYTE *value)
{
   if (mp == NULL)
     return -1;

   /* TLB cached is random access by native */
   *value = mp->storage[addr];

   return 0;
}


/*
 *  TLBMEMPHY_write natively supports MEMPHY device interfaces
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int TLBMEMPHY_write(struct memphy_struct * mp, int addr, BYTE data)
{
   if (mp == NULL)
     return -1;

   /* TLB cached is random access by native */
   mp->storage[addr] = data;

   return 0;
}

/*
 *  TLBMEMPHY_format natively supports MEMPHY device interfaces
 *  @mp: memphy struct
 */


int TLBMEMPHY_dump(struct memphy_struct * mp)
{
   /*TODO dump memphy contnt mp->storage 
    *     for tracing the memory content
    */

   return 0;
}


/*
 *  Init TLBMEMPHY struct
 */
int init_tlbmemphy(struct memphy_struct *mp, int max_size)
{
   mp->storage = (BYTE *)malloc(max_size*sizeof(BYTE));
   mp->maxsz = max_size;

   mp->rdmflg = 1;
   pthread_mutex_init(&tlb_lock, NULL);
   return 0;
}

int TLBMEMPHY_destroy_lock() {
   pthread_mutex_destroy(&tlb_lock);
   return 0;
}
//#endif
