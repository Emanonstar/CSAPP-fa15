/*
 * mm_explicit-free-list.c
 * 
 * In this approach, a block has a header and a footer.
 * The list of free blocks is a doublely-linked list, every free 
 * block has a PRED(predecessor) ptr and a SUCC(successor) ptr.
 * A LIFO ordering and a first-fit placement policy are adopted.
 * Blocks are splitted and  coalesced immediately. 
 * Realloc is implemented directly using mm_malloc and mm_free.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "YXYX",
    /* First member's full name */
    "Li Yuxing",
    /* First member's email address */
    "yuxingli@pku.edu.cn",
    /* Second member's full name (leave blank if none) */
    "Wu Yingxi",
    /* Second member's email address (leave blank if none) */
    "winedia@gmail.com"
};

// #define DEBUG

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4             /* Word and header/footer size (bytes) */
#define DSIZE 8             /* Double word size (bytes) */
#define CHUNKSIZE (1<<12)   /* Extend heap by this amoumt */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)      (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read and write a double word at address p */
#define GET_D(p)      ((void *)(*(unsigned long *)(p)))
#define PUT_D(p, val) (*(unsigned long *)(p) = (unsigned long)(val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of nexr and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Given a free block ptr bp, get ptr points to address of pred and succ blocks */
#define PRED_ADRP(bp) (bp)
#define SUCC_ADRP(bp) ((char *)(bp) + DSIZE)

#ifdef DEBUG
#define CHECKHEAP int ret; if ((ret = mm_check()) != 0) printf("%s %d: error %d\n", __func__, __LINE__, ret), exit(1)
#else
#define CHECKHEAP 0
#endif

/* Ptr points to the prologue block */
static char *heap_listp;

/*Ptr points to the root of explicit free list */
static char **root;

/*Prototypes of helper functions */
static void *extend_heap(size_t words);
static void *coalesce(void *ptr);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void delete_block(void *bp);
static void insert_block_at_beginning(void *bp);
static int list_contains(void *bp);
inline size_t adjust(size_t size);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(6 * WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);                             /* Allignment padding */
    PUT(heap_listp +(1*WSIZE), PACK(2*DSIZE, 1));   /* Prologue header */
    PUT(heap_listp +(4*WSIZE), PACK(2*DSIZE, 1));   /* Prologue footer */
    PUT(heap_listp +(5*WSIZE), PACK(0, 1));         /* Epilogue header */
    heap_listp += DSIZE;
    root = (char **)(heap_listp);
    *root = NULL;
    
    /* Extend the empty heap with a free block of CHUNKSIZE words */
    if (extend_heap(CHUNKSIZE) == NULL)
        return -1;
    return 0;
}

/* 
 * mm_check - scan the heap and check it for consistency.
 */
static int mm_check(void)
{
    void *p;
    void *lastp;

    /* Check free list contains no allocated blocks */ 
    for (p = *root; p != NULL; p = GET_D(SUCC_ADRP(p)))
    {
        if (GET_ALLOC(HDRP(p)))
            return 1;
    }

    /* Check all free blocks are in the free list */ 
    for (p = heap_listp; GET_SIZE(HDRP(p)) > 0; p = NEXT_BLKP(p))
    {
        if (!GET_ALLOC(HDRP(p)) && !list_contains(p)) 
            return 2;
    }

    /* Check no contiguous free blocks in memory */ 
    size_t last_alloc = 1;
    for (p = heap_listp; GET_SIZE(HDRP(p)) > 0; p = NEXT_BLKP(p))
    {
        if (!GET_ALLOC(HDRP(p)) && !last_alloc)
            return 3;
        last_alloc = GET_ALLOC(HDRP(p));
    }

    /* Check pred/succ pointers in free	blocks are consistent */ 
    lastp = NULL;
    for (p = *root; p != NULL; p = GET_D(SUCC_ADRP(p)))
    {
        if (GET_D(PRED_ADRP(p)) != lastp)
            return 4;
        lastp = p;
    }
    return 0;
}

/* Check if the free list contains the block(bp) */ 
static int list_contains(void *bp)
{
    void *p;
    for (p = *root; p != NULL; p = GET_D(SUCC_ADRP(p)))
    {
        if (p == bp) {
            return 1;
        }
    }
    return 0;
}

/* Extend the heap with a free block of words WSIZE bytes. */
static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long) (bp = mem_sbrk(size)) == -1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));           /* Free block header */
    PUT(FTRP(bp), PACK(size, 0));           /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   /* New epilogue header */

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}

/* Merge the free block(ptr) with its adjacent free blocks. */
static void *coalesce(void *ptr) 
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDRP(ptr));

    if (prev_alloc && next_alloc) {                 /* Case 1 */
        /* do nothing */
    }

    else if (prev_alloc && !next_alloc) {           /* Case 2 */
        delete_block(NEXT_BLKP(ptr));
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc) {           /* Case 3 */
        delete_block(PREV_BLKP(ptr));
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
        PUT(FTRP(ptr), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);  
    }

    else {                                          /* Case 4 */
        delete_block(NEXT_BLKP(ptr));
        delete_block(PREV_BLKP(ptr));
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr))) +
            GET_SIZE(HDRP(PREV_BLKP(ptr)));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }

    insert_block_at_beginning(ptr);
    CHECKHEAP;
    return ptr;
}

/* Delete the block(bp) from the free list. */
static void delete_block(void *bp)
{
    void *pred = GET_D(PRED_ADRP(bp));
    void *succ = GET_D(SUCC_ADRP(bp));
    if (pred != NULL) {
        PUT_D(SUCC_ADRP(pred), succ);
    } else {
        PUT_D(root, succ);
    }
    if (succ != NULL){
        PUT_D(PRED_ADRP(succ), pred);
    }
}

/* Insert the block(bp) at the beginning of the free list. */
static void insert_block_at_beginning(void *bp)
{
    void *old_beginning = GET_D(root);
    PUT_D(SUCC_ADRP(bp), old_beginning);
    PUT_D(bp, NULL);
    if (old_beginning != NULL) 
        PUT_D(PRED_ADRP(old_beginning), bp);
    PUT_D(root, bp);
    CHECKHEAP;
}

/* 
 * mm_malloc - Allocate a block with first-fit.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    asize = adjust(size);

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp, asize);

    return bp;
}

/* Adjust block size to include overhead and alignment reqs. */
inline size_t adjust(size_t size)
{
    size_t asize;
    if (size < 2 * DSIZE)
        asize = 3 * DSIZE;
    else 
        asize = ALIGN((size + DSIZE));
    return asize;
}

/* First-fit search */
static void *find_fit(size_t asize)
{
    void *p;
    for (p = *root; p != NULL; p = GET_D(SUCC_ADRP(p)))
    {
        if (GET_SIZE(HDRP(p)) >= asize)
            return p;
    }
    return NULL;    /* No fit */
}

/* Place a block of asize by splitting(optionally) and updating the free list. */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    size_t left = csize - asize;
    // void *pred = GET_D(PRED_ADRP(bp));
    // void *succ = GET_D(SUCC_ADRP(bp));

    if (left >= (3 *DSIZE)) {
        delete_block(bp);
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(left, 0));
        PUT(FTRP(bp), PACK(left, 0));
        insert_block_at_beginning(bp);
        // if (pred != NULL) {
        //     PUT_D(SUCC_ADRP(pred), bp);
        // } else {
        //     PUT_D(root, bp);
        // }
        // PUT_D(PRED_ADRP(bp), pred);
        // PUT_D(SUCC_ADRP(bp), succ);
        // if (succ != NULL)
        //     PUT_D(PRED_ADRP(succ), bp);
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        delete_block(bp);
    }
    CHECKHEAP;
}

/*
 * mm_free - Freeing a block updates its header and footer and 
 *      optionally merge with its adjacent free blocks.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}
