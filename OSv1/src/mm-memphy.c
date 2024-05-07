//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */

#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

pthread_mutex_t ram_lock;
pthread_mutex_t swp_lock;

int init_memphy_lock() 
{
    pthread_mutex_init(&ram_lock, NULL);
    pthread_mutex_init(&swp_lock, NULL);
    return 0;
}

int destroy_memphy_lock() 
{
    pthread_mutex_destroy(&ram_lock);
    pthread_mutex_destroy(&swp_lock);
    return 0;
}

/*
 *  MEMPHY_mv_csr - move MEMPHY cursor
 *  @mp: memphy struct
 *  @offset: offset
 */
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
   else {
      /* Sequential access device */
      int val = MEMPHY_seq_write(mp, addr, data);
      pthread_mutex_unlock(lock);
      return val;
   }
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

/**
 * Get a free frame from the given MEMPHY struct.
 *
 * @param mp The MEMPHY struct.
 * @param retfpn Pointer to an integer where the frame number will be stored.
 * @param option The option for locking (RAM_LCK or SWP_LCK).
 *
 * @return 0 on success, -1 on failure (if no free frames are available).
 */
int MEMPHY_get_freefp(struct memphy_struct *mp, int *retfpn, BYTE option)
{
   /* Select the lock based on the option */
   pthread_mutex_t *lock;

   /* Switch statement to determine the lock to be used */
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

   /* Lock the mutex */
   pthread_mutex_lock(lock);

   /* If there are no free frames, return failure */
   if (mp->free_fp_list == NULL){
      pthread_mutex_unlock(lock);
      return -1;
   }

   /* Get the free frame from the list */
   struct framephy_struct *fp = mp->free_fp_list;

   /* Store the frame number in the provided pointer */
   *retfpn = fp->fpn;

   /* Update the free frames list */
   mp->free_fp_list = fp->fp_next;

   /* The MEMPHY is iteratively used up until it's exhausted
    * No garbage collector acting then it's not released
    */
   free(fp);

   /* Unlock the mutex */
   pthread_mutex_unlock(lock);

   return 0;
}


/**
 * Get a used frame from the given MEMPHY struct.
 *
 * @param mp The MEMPHY struct.
 * @param fpn The frame number to be retrieved.
 * @param option The option for locking (RAM_LCK or SWP_LCK).
 *
 * @return 0 on success, -1 on failure.
 */
int MEMPHY_get_usedfp(struct memphy_struct *mp, int fpn, BYTE option) {
   /* Select the lock based on the option */
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
   /* Lock the mutex */
   pthread_mutex_lock(lock);
   /* Get the used frame list */
   struct framephy_struct *usedframe = mp->used_fp_list;
   /* If the used frame list is empty, return error */
   if (usedframe == NULL) {
      pthread_mutex_unlock(lock);
      return -1;
   }
   /* If the frame number matches, remove the frame from the used frame list and free it */
   if (usedframe->fpn == fpn) {
      mp->used_fp_list = usedframe->fp_next;
      if (mp->used_fp_list == NULL)
      mp->used_fp_tail = NULL;
      free(usedframe);
   } else {
      /* If the frame number does not match, find the frame and remove it from the used frame list and free it */
      while (usedframe->fp_next != NULL 
            && usedframe->fp_next->fpn != fpn) {
         usedframe = usedframe->fp_next;
      }
      if (usedframe->fp_next != NULL) {
         struct framephy_struct *node = usedframe->fp_next;
         usedframe->fp_next = node->fp_next;
         if (node == mp->used_fp_tail)
            mp->used_fp_tail = usedframe;
         free(node);
      }
   }
   /* Unlock the mutex */
   pthread_mutex_unlock(lock);
   return 0;
}


/**
 * Adds a used frame to the used frame list of the given MEMPHY struct.
 *
 * @param mp The MEMPHY struct.
 * @param fpn The frame number.
 * @param owner The owner of the frame.
 * @param pgn The page number.
 * @param option The option for locking (RAM_LCK or SWP_LCK).
 *
 * @return 0 on success, -1 on failure.
 */
int MEMPHY_put_usedfp(struct memphy_struct *mp, int fpn, struct mm_struct *owner, int pgn, BYTE option) {
   /* Allocate memory for the frame */
   struct framephy_struct *fp = malloc(sizeof(struct framephy_struct));
   /* Set the frame number */
   fp->fpn = fpn;
   /* Set the next frame to NULL */
   fp->fp_next = NULL;
   /* Set the owner of the frame */
   fp->owner = owner;
   /* Set the page number of the frame */
   fp->pgn = pgn;
   /* Select the lock based on the option */
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
   /* Lock the mutex */
   pthread_mutex_lock(lock);
   /* If the used frame list is empty, add the frame to the list and set it as the tail */
   if (mp->used_fp_list == NULL) {
      mp->used_fp_list = fp;
      mp->used_fp_tail = fp;
   } else {
      /* Add the frame to the end of the used frame list */
      mp->used_fp_tail->fp_next = fp;
      /* Set the frame as the new tail */
      mp->used_fp_tail = fp;
   }
   /* Unlock the mutex */
   pthread_mutex_unlock(lock);
   /* Return success */
   return 0;
}

int MEMPHY_dump(struct memphy_struct * mp, int fpn, int start, int end)
{
   if (mp == NULL || fpn < 0)
      return -1;

   
   if (start == -1 && end == -1)
   {
      printf("Dumping memory content:\n");
      for (int i = 0; i < mp->maxsz; i++)
      {
         printf("Address %d: %2x\n", i, mp->storage[i]);
      }
   }
   else
   {
      
      if (start < 0 || end > PAGING_PAGESZ || start > end)
         return -1;

      
      if (end == -1)
         end = PAGING_PAGESZ;

      
      printf("MEMPHY dumped for frame %d from %d to %d:\n", fpn, start, end - 1);
      for (int i = start; i < end; i++)
      {
         int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + i;
         printf("%d: %2x\n", phyaddr, mp->storage[phyaddr]);
      }
   }

   return 0;
}
/**
 * MEMPHY_put_freefp - put a free frame into the free frame list
 * @mp: pointer to memphy struct
 * @fpn: frame number of the free frame
 * @option: option for locking (RAM_LCK or SWP_LCK)
 * 
 * This function puts a free frame into the free frame list.
 *
 * Return: 0 on success, -1 on error
 */
int MEMPHY_put_freefp(struct memphy_struct *mp, int fpn, BYTE option)
{
   /* Lock for the free frame list */
   pthread_mutex_t *lock;

   /* Select the lock based on the option */
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

   /* Lock the selected lock */
   pthread_mutex_lock(lock);

   /* Create a new node for the free frame */
   struct framephy_struct *newnode = malloc(sizeof(struct framephy_struct));
   newnode->fpn = fpn;
   
   /* Add the new node to the head of the free frame list */
   newnode->fp_next = mp->free_fp_list;
   mp->free_fp_list = newnode;

   /* Unlock the selected lock */
   pthread_mutex_unlock(lock);

   return 0;
}

/**
 * MEMPHY_pop_usedfp - pop a used frame from the used frame list
 * @mp: pointer to memphy struct
 * @fpn: pointer to a variable to store the frame number
 * @pgn: pointer to a variable to store the page number
 * @mm: pointer to a pointer to a mm_struct struct to store the owner
 * @option: option for locking (RAM_LCK or SWP_LCK)
 * 
 * This function pops the first used frame from the used frame list,
 * stores the frame number, page number, and owner in the respective pointers,
 * updates the used frame list, and frees the used frame. It returns 0 on success,
 * or -1 if the used frame list is empty.
 *
 * Return: 0 on success, -1 on error
 */
int MEMPHY_pop_usedfp(struct memphy_struct *mp, int *fpn, int *pgn, struct mm_struct **mm, BYTE option) {
   pthread_mutex_t *lock;
   /* Select the lock based on the option */
   switch (option)
   {
   case RAM_LCK:
      lock = &ram_lock;
      break;
   case SWP_LCK:
      lock = &swp_lock;
      break;   /* Select the lock based on the option */
   default:
      return -1; /* Return error if the option is not valid */
   }
   pthread_mutex_lock(lock);
   struct framephy_struct *usedframe = mp->used_fp_list;
   if (usedframe == NULL){ /* If the used frame list is empty, return error */
      pthread_mutex_unlock(lock);
      return -1;
   }
   /* Store the values from the used frame to the respective pointers */
   *fpn = usedframe->fpn;
   *pgn = usedframe->pgn;
   *mm = usedframe->owner;
   /* Update the used frame list */
   mp->used_fp_list = usedframe->fp_next;
   if (mp->used_fp_list == NULL)
      mp->used_fp_tail = NULL;
   /* Free the used frame */
   free(usedframe);
   pthread_mutex_unlock(lock);
   return 0; /* Return success */
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

int destroy_memphy(struct memphy_struct *mp) {
   if (mp == NULL || mp->maxsz <= 0)
    return -1;
   free(mp->storage);

   while (mp->free_fp_list != NULL) {
      struct framephy_struct *fp = mp->free_fp_list;
      mp->free_fp_list = fp->fp_next;
      free(fp);
   }
   
   while (mp->used_fp_list != NULL) {
      struct framephy_struct *fp = mp->used_fp_list;
      mp->used_fp_list = fp->fp_next;
      free(fp);
   }
   mp->used_fp_tail = NULL;
   return 0;
}

//#endif
