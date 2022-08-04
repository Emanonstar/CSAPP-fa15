/*
 * mm_explicit-free-list.c
 * 
 * In this approach, a block has a header and a footer.
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
    "solo",
    /* First member's full name */
    "Li Yuxing",
    /* First member's email address */
    "yuxingli@pku.edu.cn",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

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

/* Given a free block ptr bp, get ptr points to address of succ block */
#define SUCCP(bp) ((char *)(bp) + DSIZE)

/* Ptr points to the prologue block */
static char *heap_listp;

/*Ptr points to the root of explicit free list */
static char **root;

static void *extend_heap(size_t words);
static void *coalesce(void *ptr);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(8 * WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);                         /* Allignment padding */
    PUT(heap_listp +(1*WSIZE), PACK(4 * DSIZE, 1)); /* Prologue header */
    PUT(heap_listp +(6*WSIZE), PACK(4 * DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp +(7*WSIZE), PACK(0, 1));     /* Epilogue header */
    heap_listp += DSIZE;
    root = (char **)(heap_listp + DSIZE);
    *(unsigned long *)(heap_listp) = 0;
    *root = NULL;
    
    /*Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE) == NULL)
        return -1;
    return 0;
}


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

    // PUT_D(SUCCP(bp), *((unsigned long *) root));
    // PUT_D(root, bp);

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}

static void *coalesce(void *ptr) 
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDRP(ptr));
    void *pred;
    void *succ;
    // printf("Coalescing %p size %x prev_alloc: %u next_alloc: %u\n", ptr, size, prev_alloc, next_alloc);

    if (prev_alloc && next_alloc) {                 /* Case 1 */
        // PUT_D(SUCCP(ptr), *((unsigned long *) root));
        // PUT_D(ptr, NULL);
        // PUT_D(root, ptr);
        // return ptr;
    }

    else if (prev_alloc && !next_alloc) {           /* Case 2 */
        pred = GET_D(NEXT_BLKP(ptr));
        succ = GET_D(SUCCP(NEXT_BLKP(ptr)));
        // printf("  %p size: %x pred: %p, succ: %p\n", NEXT_BLKP(ptr), GET_SIZE(HDRP(NEXT_BLKP(ptr))), pred, succ);
        if (pred != NULL) {
            PUT_D(SUCCP(pred), succ);
        } else {
            PUT_D(root, succ);
        }
        if (succ != NULL){
            PUT_D(succ, pred);
        }

        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc) {           /* Case 3 */
        pred = GET_D(PREV_BLKP(ptr));
        succ = GET_D(SUCCP(PREV_BLKP(ptr)));
        // printf("  %p pred: %p, succ: %p\n", PREV_BLKP(ptr), pred, succ);
        if (pred != NULL) {
            PUT_D(SUCCP(pred), succ);
        } else {
            PUT_D(root, succ);
        }
        if (succ != NULL){
            PUT_D(succ, pred);
        }

        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
        PUT(FTRP(ptr), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);  
    }

    else {                                          /* Case 4 */
        pred = GET_D(NEXT_BLKP(ptr));
        succ = GET_D(SUCCP(NEXT_BLKP(ptr)));
        // printf("  %p pred: %p, succ: %p\n", NEXT_BLKP(ptr), pred, succ);
        if (pred != NULL) {
            PUT_D(SUCCP(pred), succ);
        } else {
            PUT_D(root, succ);
        }
        if (succ != NULL){
            PUT_D(succ, pred);
        }
        
        pred = GET_D(PREV_BLKP(ptr));
        succ = GET_D(SUCCP(PREV_BLKP(ptr)));
        // printf("  %p pred: %p, succ: %p\n", PREV_BLKP(ptr), pred, succ);
        if (pred != NULL) {
            PUT_D(SUCCP(pred), succ);
        } else {
            PUT_D(root, succ);
        }
        if (succ != NULL){
            PUT_D(succ, pred);
        }

        size += GET_SIZE(HDRP(NEXT_BLKP(ptr))) +
            GET_SIZE(HDRP(PREV_BLKP(ptr)));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }

    PUT_D(SUCCP(ptr), *((unsigned long *) root));
    PUT_D(ptr, NULL);
    if (*root != NULL) 
        PUT_D(*root, ptr);
    PUT_D(root, ptr);
    // printf("After coalescing, ptr: %p, size: %x\n\n", ptr, GET_SIZE(HDRP(ptr)));
    return ptr;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
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
    if (size < 2 * DSIZE)
        asize = 3 * DSIZE;
    else 
        asize = ALIGN((size + DSIZE));
    
    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        // printf("malloc %d at %p, current root %p\n", asize, bp, *root);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp, asize);

    // printf("malloc %d at %p, current root %p\n", asize, bp, *root);
    return bp;
}

/* First-fit search */
static void *find_fit(size_t asize)
{
    void *p;
    for (p = *root; p != NULL; p = GET_D(SUCCP(p)))
    {
        // printf("p: %p, PRED: %p, SUCC: %p, HDR: %d\n", p, GET_D(p), GET_D(SUCCP(p)), GET_SIZE(HDRP(p)));
        if (!GET_ALLOC(HDRP(p)) && (GET_SIZE(HDRP(p)) >= asize)) {
            return p;
        }
    }
    return NULL;    /* No fit */
}

static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    size_t left = csize - asize;
    void *pred = GET_D(bp);
    void *succ = GET_D(SUCCP(bp));

    if (left >= (3 *DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(left, 0));
        PUT(FTRP(bp), PACK(left, 0));
        if (pred != NULL) {
            PUT_D(SUCCP(pred), bp);
        } else {
            PUT_D(root, bp);
        }
        PUT_D(bp, pred);
        PUT_D(SUCCP(bp), succ);
        if (succ != NULL)
            PUT_D(succ, bp);
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        if (pred != NULL) {
            PUT_D(SUCCP(pred), succ);
        } else {
            PUT_D(root, succ);
        }  
        if (succ != NULL)
            PUT_D(succ, pred);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    // printf("freeing %p\n", ptr);
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    // PUT_D(SUCCP(ptr), *((unsigned long *) root));
    // PUT_D(root, ptr);
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
