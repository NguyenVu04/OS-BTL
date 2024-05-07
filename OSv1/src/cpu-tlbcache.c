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

/**
 * Read from the TLB cache device.
 *
 * @param mp The memory struct.
 * @param pid The process ID.
 * @param pgnum The page number.
 * @param value The value to write.
 *
 * @return 0 on success, -1 if the tag does not match.
 *
 * @throws None.
 */
int tlb_cache_read(struct memphy_struct * mp, int pid, int pgnum, uint32_t *value)
{
   /* Cast the storage to uint32_t*. */
   uint32_t* storage = (uint32_t*) mp->storage;
   
   /* Calculate the index. */
   uint32_t i = TLB_INDEX(pid, pgnum);
   
   /* Calculate the id and tag. */
   uint32_t id = (i % (mp->maxsz / 8)) * 2;
   uint32_t tag = i / (mp->maxsz / 8);
   
   /* Lock the TLB cache. */
   pthread_mutex_lock(&tlb_lock);
   
   /* Check if the tag matches. */
   if (storage[id] != tag) {
      /* Unlock and return -1. */
      pthread_mutex_unlock(&tlb_lock);
      return -1;
   }
   
   /* Store the value and unlock. */
   *value = storage[id + 1];
   pthread_mutex_unlock(&tlb_lock);
   
   /* Return 0. */
   return 0;
}

/**
 * Write to TLB cache device
 *
 * @param mp The memphy struct
 * @param pid The process id
 * @param pgnum The page number
 * @param value The value to write
 *
 * @return 0 on success
 */
int tlb_cache_write(struct memphy_struct *mp, int pid, int pgnum, uint32_t value)
{
   uint32_t* storage = (uint32_t*) mp->storage;
   uint32_t i = TLB_INDEX(pid, pgnum);
   uint32_t id = (i % (mp->maxsz / 8)) * 2;
   uint32_t tag = i / (mp->maxsz / 8);

   /* Lock the TLB cache */
   pthread_mutex_lock(&tlb_lock);

   /* Store the tag and value */
   storage[id] = tag;
   storage[id + 1] = value;

   /* Unlock the TLB cache */
   pthread_mutex_unlock(&tlb_lock);

   /* Return success */
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

/**
 * TLBMEMPHY_dump prints the TLB cache entry for the given process and page number
 *
 * @param mp The TLBMEMPHY struct
 * @param pid The process id
 * @param pgnum The page number
 *
 * @return 0 on success
 */
int TLBMEMPHY_dump(struct memphy_struct * mp, int pid, int pgnum)
{
   /* Cast storage to uint32_t *. */
   uint32_t *storage = (uint32_t *)mp->storage;
   
   /* Calculate index. */
   uint32_t i = TLB_INDEX(pid, pgnum);
   uint32_t id = (i % (mp->maxsz / 8)) * 2;
   /* Print the TLB cache entry. */
   printf("TLBMEMPHY dump:\n");
   printf("%08x: %08x\n", storage[id], storage[id + 1]);
   
   /* Return 0. */
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

int destroy_tlbmemphy(struct memphy_struct *mp) {
   if (mp == NULL)
      return -1;
   free(mp->storage);
   pthread_mutex_destroy(&tlb_lock);
   return 0;
}
//#endif
