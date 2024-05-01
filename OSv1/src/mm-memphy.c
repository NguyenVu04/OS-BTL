//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */

#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
/*
 *  MEMPHY_mv_csr - move MEMPHY cursor
 *  @mp: memphy struct
 *  @offset: offset
 */

pthread_mutex_t ram_lock;
pthread_mutex_t swp_lock;

int init_memphy_lock() {
    pthread_mutex_init(&ram_lock, NULL);
    pthread_mutex_init(&swp_lock, NULL);
    return 0;
}

int destroy_memphy_lock() {
    pthread_mutex_destroy(&ram_lock);
    pthread_mutex_destroy(&swp_lock);
    return 0;
}

int MEMPHY_mv_csr(struct memphy_struct *mp, int offset)
{
   int numstep = 0;

   mp->cursor = 0;
   while(numstep < offset && numstep < mp->maxsz){
     /* Traverse sequentially */
     mp->cursor = (mp->cursor + 1) % mp->maxsz;
     numstep++;
   }

   return 0;
}

/*
 *  MEMPHY_seq_read - read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_seq_read(struct memphy_struct *mp, int addr, BYTE *value)
{
   if (mp == NULL)
     return -1;

   if (!mp->rdmflg)
     return -1; /* Not compatible mode for sequential read */

   MEMPHY_mv_csr(mp, addr);
   *value = (BYTE) mp->storage[addr];

   return 0;
}

/*
 *  MEMPHY_read read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_read(struct memphy_struct *mp, int addr, BYTE *value)
{
   if (mp == NULL)
     return -1;
     
   if (mp->rdmflg)
      *value = mp->storage[addr];
   else /* Sequential access device */
      return MEMPHY_seq_read(mp, addr, value);
   return 0;
}

/*
 *  MEMPHY_seq_write - write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_seq_write(struct memphy_struct * mp, int addr, BYTE value)
{

   if (mp == NULL)
     return -1;

   if (!mp->rdmflg)
     return -1; /* Not compatible mode for sequential read */

   MEMPHY_mv_csr(mp, addr);
   mp->storage[addr] = value;

   return 0;
}

/*
 *  MEMPHY_write-write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_write(struct memphy_struct *mp, int addr, BYTE data, BYTE option)
{
   if (mp == NULL)
     return -1;
   pthread_mutex_t *lock;
   switch (option)
   {
   case RAM_LCK:
      lock = &ram_lock;
      break;
   case SWP_LCK:
      lock = &swp_lock;
      break;   
   default:
      return -1;
   }
   pthread_mutex_lock(lock);
   if (mp->rdmflg)
      mp->storage[addr] = data;
   else /* Sequential access device */
      return MEMPHY_seq_write(mp, addr, data);
   pthread_mutex_unlock(lock);
   return 0;
}

/*
 *  MEMPHY_format-format MEMPHY device
 *  @mp: memphy struct
 */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
    /* This setting come with fixed constant PAGESZ */
    int numfp = mp->maxsz / pagesz;
    struct framephy_struct *newfst, *fst;
    int iter = 0;

    if (numfp <= 0)
      return -1;

    /* Init head of free framephy list */ 
    fst = malloc(sizeof(struct framephy_struct));
    fst->fpn = iter;
    mp->free_fp_list = fst;
    mp->used_fp_list = NULL;
    mp->used_fp_tail = NULL;
    /* We have list with first element, fill in the rest num-1 element member*/
    for (iter = 1; iter < numfp ; iter++)
    {
       newfst =  malloc(sizeof(struct framephy_struct));
       newfst->fpn = iter;
       newfst->owner = NULL;
       newfst->pgn = 0;
       newfst->fp_next = NULL;
       fst->fp_next = newfst;
       fst = newfst;
    }

    return 0;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, int *retfpn, BYTE option)
{
   pthread_mutex_t *lock;
   switch (option)
   {
   case RAM_LCK:
      lock = &ram_lock;
      break;
   case SWP_LCK:
      lock = &swp_lock;
      break;   
   default:
      return -1;
   }
   pthread_mutex_lock(lock);
   if (mp->free_fp_list == NULL){
      pthread_mutex_unlock(lock);
      return -1;
   }
   
   struct framephy_struct *fp = mp->free_fp_list;
   *retfpn = fp->fpn;
   mp->free_fp_list = fp->fp_next;

   /* MEMPHY is iteratively used up until its exhausted
    * No garbage collector acting then it not been released
    */
   free(fp);
   pthread_mutex_unlock(lock);
   return 0;
}

int MEMPHY_get_usedfp(struct memphy_struct *mp, int fpn, BYTE option) {
   pthread_mutex_t *lock;
   switch (option)
   {
   case RAM_LCK:
      lock = &ram_lock;
      break;
   case SWP_LCK:
      lock = &swp_lock;
      break;   
   default:
      return -1;
   }
   pthread_mutex_lock(lock);
   struct framephy_struct *usedframe = mp->used_fp_list;
   if (usedframe == NULL) {
      pthread_mutex_unlock(lock);
      return -1;
   }
   if (usedframe->fpn == fpn) {
      // Remove the frame from the used frame list
      mp->used_fp_list = usedframe->fp_next;
      if (mp->used_fp_list == NULL)
      mp->used_fp_tail = NULL;
      // Free the frame
      free(usedframe);
   } else {
      // Remove the frame from the used frame list
      while (usedframe->fp_next != NULL 
            && usedframe->fp_next->fpn != fpn) {
      usedframe = usedframe->fp_next;
      }
      if (usedframe->fp_next != NULL) {
      struct framephy_struct *node = usedframe->fp_next;
      usedframe->fp_next = node->fp_next;
      if (node == mp->used_fp_tail)
         mp->used_fp_tail = usedframe;
      // Free the frame
      free(node);
      }
   }
   pthread_mutex_unlock(lock);
   return 0;
}

int MEMPHY_put_usedfp(struct memphy_struct *mp, int fpn, struct mm_struct *owner, int pgn, BYTE option) {
   struct framephy_struct *fp = malloc(sizeof(struct framephy_struct));
   fp->fpn = fpn;
   fp->fp_next = NULL;
   fp->owner = owner;
   fp->pgn = pgn;
   pthread_mutex_t *lock;
   switch (option)
   {
   case RAM_LCK:
      lock = &ram_lock;
      break;
   case SWP_LCK:
      lock = &swp_lock;
      break;   
   default:
      return -1;
   }
   pthread_mutex_lock(lock);
   if (mp->used_fp_list == NULL) {
      mp->used_fp_list = fp;
      mp->used_fp_tail = fp;
   } else {
      mp->used_fp_tail->fp_next = fp;
      mp->used_fp_tail = fp;
   }
   pthread_mutex_unlock(lock);
   return 0;
}

int MEMPHY_dump(struct memphy_struct * mp)
{
   return 0;
}
int MEMPHY_put_freefp(struct memphy_struct *mp, int fpn, BYTE option)
{
   pthread_mutex_t *lock;
   switch (option)
   {
   case RAM_LCK:
      lock = &ram_lock;
      break;
   case SWP_LCK:
      lock = &swp_lock;
      break;   
   default:
      return -1;
   }
   pthread_mutex_lock(lock);
   struct framephy_struct *fp = mp->free_fp_list;
   struct framephy_struct *newnode = malloc(sizeof(struct framephy_struct));
   /* Create new node with value fpn */
   newnode->fpn = fpn;
   newnode->fp_next = fp;
   mp->free_fp_list = newnode;
   pthread_mutex_unlock(lock);
   return 0;
}

int MEMPHY_pop_usedfp(struct memphy_struct *mp, int *fpn, int *pgn, struct mm_struct **mm, BYTE option) {
   pthread_mutex_t *lock;
   switch (option)
   {
   case RAM_LCK:
      lock = &ram_lock;
      break;
   case SWP_LCK:
      lock = &swp_lock;
      break;   
   default:
      return -1;
   }
   pthread_mutex_lock(lock);
   struct framephy_struct *usedframe = mp->used_fp_list;
   if (usedframe == NULL){
      pthread_mutex_unlock(lock);
      return -1;
   }
   *fpn = usedframe->fpn;
   *pgn = usedframe->pgn;
   *mm = usedframe->owner;
   mp->used_fp_list = usedframe->fp_next;
   if (mp->used_fp_list == NULL)
      mp->used_fp_tail = NULL;
   free(usedframe);
   pthread_mutex_unlock(lock);
   return 0;
}

/*
 *  Init MEMPHY struct
 */
int init_memphy(struct memphy_struct *mp, int max_size, int randomflg)
{
   mp->storage = (BYTE *)malloc(max_size*sizeof(BYTE));
   mp->maxsz = max_size;

   MEMPHY_format(mp,PAGING_PAGESZ);

   mp->rdmflg = (randomflg != 0)?1:0;

   if (!mp->rdmflg )   /* Not Ramdom acess device, then it serial device*/
      mp->cursor = 0;

   return 0;
}

//#endif
