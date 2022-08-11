/*
 * mm_segregated-list.c
 * 
 * In this approach, thre are some segregated free block lists,
 * Every block is a doublely-linked list. Every block has a 
 * header and a footer and a free block has a PRED(predecessor) ptr 
 * and a SUCC(successor) ptr.
 * A LIFO ordering and a first-fit placement policy are adopted.
 * Blocks are splitted and  coalesced immediately. 
 * Realloc is implemented using mm_malloc and mm_free woth some
 * improvements.
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

// #define DEBUG Uncomment to use DEBUG mode 

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define WSIZE 4                 /* Word and header/footer size (bytes) */
#define DSIZE 8                 /* Double word size (bytes) */
#define CHUNKSIZE (1<<12)       /* Extend heap by this amoumt */
#define INITCHUNKSIZE (1<<6)    /* Initialize heap by this amount */
#define BUCKETSIZE 18           /* Number of seglist buckets */
#define THRESHOLD 80            /* Paarameter used by place()*/

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
#define PRINTHEAP print_heap()
#else
#define CHECKHEAP 0
#define PRINTHEAP 0
#endif

/* Given index, compute address of corresponding root */
#define ROOT(index) ((char *)(heap_listp) + ((index) * DSIZE))

/* Ptr points to the prologue block */
static char *heap_listp;

/*Prototypes of helper functions */
static void *extend_heap(size_t words);
static void *coalesce(void *ptr);
static void *find_fit(size_t asize);
static void *place(void *bp, size_t asize);
static void delete_block(void *bp);
static void insert_block(void *bp);
static void insert_block_at_beginning(void *bp);
inline size_t adjust(size_t size);
static int index_of(size_t asize);
static int mm_check(void);
static int list_contains(void *bp);
static void print_heap(void);
static void place_r(void *bp, size_t asize);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk((BUCKETSIZE+2) * DSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);                                                     /* Allignment padding */
    PUT(heap_listp +(1*WSIZE), PACK((BUCKETSIZE+1)*DSIZE, 1));              /* Prologue header */
    PUT(heap_listp +((BUCKETSIZE+1)*DSIZE), PACK((BUCKETSIZE+1)*DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp +((BUCKETSIZE+1)*DSIZE+WSIZE), PACK(0, 1));              /* Epilogue header */
    heap_listp += DSIZE;
    for (unsigned int i = 0; i < BUCKETSIZE; i++) {
        PUT_D(ROOT(i), NULL);
    }
    
    /* Extend the empty heap with a free block of CHUNKSIZE words */
    if (extend_heap(INITCHUNKSIZE) == NULL)
        return -1;
    return 0;
}

static int index_of(size_t asize) 
{
    int a[BUCKETSIZE-1]={3,4,5,6,7,8,15,16,17,32,64,128,256,512,1024,2048,4096};
    int i;
    for (i = 0; i < BUCKETSIZE-1; i++) {
        if (asize <= a[i]*DSIZE) {
            return i;
        }
    }
    return BUCKETSIZE-1;
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

    insert_block(ptr);
    CHECKHEAP;
    PRINTHEAP;
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
        PUT_D(ROOT(index_of(GET_SIZE(HDRP(bp)))), succ);
    }
    if (succ != NULL) {
        PUT_D(PRED_ADRP(succ), pred);
    }
}

/* Insert the block(bp) at the beginning of the free list. */
static void insert_block_at_beginning(void *bp)
{
    void *root = ROOT(index_of(GET_SIZE(HDRP(bp))));
    void *old_beginning = GET_D(root);
    PUT_D(SUCC_ADRP(bp), old_beginning);
    PUT_D(PRED_ADRP(bp), NULL);
    if (old_beginning != NULL) 
        PUT_D(PRED_ADRP(old_beginning), bp);
    PUT_D(root, bp);
    CHECKHEAP;
}

// static void insert_block(void *bp)
// {
//     size_t size = GET_SIZE(HDRP(bp));
//     void *root = ROOT(index_of(size));
//     void *first_bp = GET_D(root);
//     void *p;

//     if (first_bp == NULL) {
//         PUT_D(PRED_ADRP(bp), NULL);
//         PUT_D(SUCC_ADRP(bp), NULL); 
//         PUT_D(root, bp);
//         CHECKHEAP;
//         return;
//     }

//     for (p = first_bp; p != NULL; p = GET_D(SUCC_ADRP(p)))
//     {
//         if (size < GET_SIZE(HDRP(p))) {
//             PUT_D(SUCC_ADRP(bp), p);
//             PUT_D(PRED_ADRP(bp), GET_D(PRED_ADRP(p)));
//             if (GET_D(PRED_ADRP(p)) != NULL) {
//                 PUT_D(SUCC_ADRP(GET_D(PRED_ADRP(p))), bp);
//             } else {
//                 PUT_D(root, bp);
//             }
//             PUT_D(PRED_ADRP(p), bp);
//             CHECKHEAP;
//             return;
//         }

//         if (GET_D(SUCC_ADRP(p)) == NULL) {
//             PUT_D(SUCC_ADRP(bp), NULL);
//             PUT_D(PRED_ADRP(bp), p);
//             PUT_D(SUCC_ADRP(p), bp);
//             CHECKHEAP;
//             return;
//         }
//     }
// }

static void insert_block(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    void *root = ROOT(index_of(size));
    void *first_bp = GET_D(root);
    void *p;

    if (first_bp == NULL) {
        PUT_D(PRED_ADRP(bp), NULL);
        PUT_D(SUCC_ADRP(bp), NULL); 
        PUT_D(root, bp);
        CHECKHEAP;
        return;
    }

    for (p = first_bp; p != NULL; p = GET_D(SUCC_ADRP(p)))
    {
        if (bp < p) {
            PUT_D(SUCC_ADRP(bp), p);
            PUT_D(PRED_ADRP(bp), GET_D(PRED_ADRP(p)));
            if (GET_D(PRED_ADRP(p)) != NULL) {
                PUT_D(SUCC_ADRP(GET_D(PRED_ADRP(p))), bp);
            } else {
                PUT_D(root, bp);
            }
            PUT_D(PRED_ADRP(p), bp);
            CHECKHEAP;
            return;
        }

        if (GET_D(SUCC_ADRP(p)) == NULL) {
            PUT_D(SUCC_ADRP(bp), NULL);
            PUT_D(PRED_ADRP(bp), p);
            PUT_D(SUCC_ADRP(p), bp);
            CHECKHEAP;
            return;
        }
    }
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
        return place(bp, asize);
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    return place(bp, asize);
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
    for (int i = index_of(asize); i < BUCKETSIZE; i++) {
        for (p = GET_D(ROOT(i)); p != NULL; p = GET_D(SUCC_ADRP(p)))
        {
            if (GET_SIZE(HDRP(p)) >= asize)
                return p;
        }
    }  
    return NULL;    /* No fit */
}

/* 
 * Place a block of asize by splitting(optionally) and updating the free list.
 * If asize less or equal than THRESHOLD, place it on the left of current block,
 * otherwise place it on the right of current block.
 */
static void *place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    size_t left = csize - asize;
    void* newbp = bp;

    if (left >= (3*DSIZE)) {
        delete_block(bp);
        if (asize <= THRESHOLD) {
            PUT(HDRP(bp), PACK(asize, 1));
            PUT(FTRP(bp), PACK(asize, 1));
            
            bp = NEXT_BLKP(bp);
            PUT(HDRP(bp), PACK(left, 0));
            PUT(FTRP(bp), PACK(left, 0));
            insert_block(bp);
        } else {
            PUT(HDRP(bp), PACK(left, 0));
            PUT(FTRP(bp), PACK(left, 0));
            newbp = NEXT_BLKP(bp);
            PUT(HDRP(newbp), PACK(asize, 1));
            PUT(FTRP(newbp), PACK(asize, 1));
            insert_block(bp);
        }
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        delete_block(bp);
    }
    CHECKHEAP;
    PRINTHEAP;
    return newbp;
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
 * mm_realloc - Implemented by some improvements
 *          based on simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    size_t old_size = GET_SIZE(HDRP(oldptr));

    /* Adjust block size to include overhead and alignment reqs. */
    size_t asize = adjust(size);        

    /* If current block is enough, return current ptr */
    if (asize <= old_size)
        return oldptr;

    /* If next block is free and total size(oldsize + next_block size) >= asize, merge */
    if (!GET_ALLOC(HDRP(NEXT_BLKP(oldptr))) && (old_size + GET_SIZE(HDRP(NEXT_BLKP(oldptr)))) >= asize) { 
        delete_block(NEXT_BLKP(oldptr));
        old_size += GET_SIZE(HDRP(NEXT_BLKP(oldptr)));
        PUT(HDRP(oldptr), PACK(old_size, 0));
        PUT(FTRP(oldptr), PACK(old_size, 0));
        place_r(oldptr,asize);
        return oldptr;
    }

    /* If current block is the last block in heap, ask for needed memoery */
    if (GET_SIZE(HDRP(NEXT_BLKP(oldptr))) == 0) { 
        if ((long) mem_sbrk(asize - old_size) == -1)
            return NULL;

        PUT(HDRP(oldptr), PACK(asize, 1));          /* Update block header */
        PUT(FTRP(oldptr), PACK(asize, 1));          /* Update block footer */
        PUT(HDRP(NEXT_BLKP(oldptr)), PACK(0, 1));   /* New epilogue header */
        return oldptr;
    }

    /* Implemented simply in terms of mm_malloc and mm_free */
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

/* place function specially for mm_realloc */
static void place_r(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    size_t left = csize - asize;
    void *newbp;

    if (left >= (3*DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(left, 0));
        PUT(FTRP(bp), PACK(left, 0));
        insert_block_at_beginning(bp);
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/* 
 * mm_check - scan the heap and check it for consistency.
 */
static int mm_check(void)
{
    void **root;
    void *p;
    void *lastp;

    /* Check free list contains no allocated blocks */ 
    for (int i = 0; i < BUCKETSIZE; i++) {
        root = ROOT(i);
        for (p = *root; p != NULL; p = GET_D(SUCC_ADRP(p)))
        {
            if (GET_ALLOC(HDRP(p)))
                return 1;
        }
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
    for (int i = 0; i < BUCKETSIZE; i++) {
        root = ROOT(i);
        lastp = NULL;
        for (p = *root; p != NULL; p = GET_D(SUCC_ADRP(p)))
        {
            if (GET_D(PRED_ADRP(p)) != lastp)
                return 4;
            lastp = p;
        }
    }
    
    return 0;
}

/* Check if the free list contains the block(bp) */ 
static int list_contains(void *bp)
{
    void **root = ROOT(index_of(GET_SIZE(HDRP(bp))));
    void *p;
    for (p = *root; p != NULL; p = GET_D(SUCC_ADRP(p)))
    {
        if (p == bp)
            return 1;
    }
    return 0;
}

/* print the entire heap */
static void print_heap(void)
{
    void *p;
    for (p = heap_listp; GET_SIZE(HDRP(p)) > 0; p = NEXT_BLKP(p))
    {
        printf("bp: %p; size: %d, alloc: %d\n",
            p, GET_SIZE(HDRP(p)), GET_ALLOC(HDRP(p)));
    }
    printf("EOF\n");
}