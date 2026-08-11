#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "buddy.h"

#define PAGE_SIZE 4096U
#define MAX_RANK 16
#define NONE (-1)

/*
 * One metadata entry is kept for every 4K page.  rank_of_page is positive
 * for a free page and negative for an allocated page; every page in a block
 * carries the block's rank, which also makes query_ranks constant time.
 */
static unsigned char *pool_base;
static int page_count;
static signed char *rank_of_page;
static unsigned char *allocation_start;
static int *next_free;
static int *prev_free;
static int free_head[MAX_RANK + 1];
static int free_count[MAX_RANK + 1];

static int valid_rank(int rank) {
    return rank >= 1 && rank <= MAX_RANK;
}

static int pages_in_rank(int rank) {
    return 1 << (rank - 1);
}

static void mark_range(int first, int rank, signed char value) {
    int i;
    int count = pages_in_rank(rank);
    for (i = first; i < first + count; ++i)
        rank_of_page[i] = value;
}

static void add_free(int first, int rank) {
    prev_free[first] = NONE;
    next_free[first] = free_head[rank];
    if (free_head[rank] != NONE)
        prev_free[free_head[rank]] = first;
    free_head[rank] = first;
    ++free_count[rank];
}

static void remove_free(int first, int rank) {
    int prev = prev_free[first];
    int next = next_free[first];
    if (prev == NONE)
        free_head[rank] = next;
    else
        next_free[prev] = next;
    if (next != NONE)
        prev_free[next] = prev;
    next_free[first] = prev_free[first] = NONE;
    --free_count[rank];
}

int init_page(void *p, int pgcount) {
    int rank, first, remaining;
    signed char *new_ranks;
    unsigned char *new_starts;
    int *new_next, *new_prev;

    if (p == NULL || pgcount <= 0)
        return -EINVAL;

    new_ranks = (signed char *)malloc((size_t)pgcount * sizeof(*new_ranks));
    new_starts = (unsigned char *)calloc((size_t)pgcount, sizeof(*new_starts));
    new_next = (int *)malloc((size_t)pgcount * sizeof(*new_next));
    new_prev = (int *)malloc((size_t)pgcount * sizeof(*new_prev));
    if (new_ranks == NULL || new_starts == NULL ||
        new_next == NULL || new_prev == NULL) {
        free(new_ranks);
        free(new_starts);
        free(new_next);
        free(new_prev);
        return -ENOSPC;
    }

    free(rank_of_page);
    free(allocation_start);
    free(next_free);
    free(prev_free);
    rank_of_page = new_ranks;
    allocation_start = new_starts;
    next_free = new_next;
    prev_free = new_prev;
    pool_base = (unsigned char *)p;
    page_count = pgcount;

    for (rank = 1; rank <= MAX_RANK; ++rank) {
        free_head[rank] = NONE;
        free_count[rank] = 0;
    }
    for (first = 0; first < pgcount; ++first) {
        next_free[first] = NONE;
        prev_free[first] = NONE;
    }

    /* Partition a non-power-of-two pool into aligned buddy blocks. */
    first = 0;
    remaining = pgcount;
    while (remaining != 0) {
        rank = MAX_RANK;
        while (pages_in_rank(rank) > remaining ||
               (first & (pages_in_rank(rank) - 1)) != 0)
            --rank;
        mark_range(first, rank, (signed char)rank);
        add_free(first, rank);
        first += pages_in_rank(rank);
        remaining -= pages_in_rank(rank);
    }
    return OK;
}

void *alloc_pages(int rank) {
    int available, first;

    if (!valid_rank(rank))
        return ERR_PTR(-EINVAL);
    for (available = rank; available <= MAX_RANK; ++available)
        if (free_head[available] != NONE)
            break;
    if (available > MAX_RANK)
        return ERR_PTR(-ENOSPC);

    first = free_head[available];
    remove_free(first, available);
    while (available > rank) {
        int right;
        --available;
        right = first + pages_in_rank(available);
        mark_range(first, available, (signed char)available);
        mark_range(right, available, (signed char)available);
        add_free(right, available);
    }
    mark_range(first, rank, (signed char)-rank);
    allocation_start[first] = 1;
    return pool_base + (size_t)first * PAGE_SIZE;
}

int return_pages(void *p) {
    uintptr_t address, base, offset;
    int first, rank;

    if (p == NULL || pool_base == NULL)
        return -EINVAL;
    address = (uintptr_t)p;
    base = (uintptr_t)pool_base;
    if (address < base)
        return -EINVAL;
    offset = address - base;
    if (offset % PAGE_SIZE != 0 || offset / PAGE_SIZE >= (uintptr_t)page_count)
        return -EINVAL;
    first = (int)(offset / PAGE_SIZE);
    if (!allocation_start[first] || rank_of_page[first] >= 0)
        return -EINVAL;

    rank = -rank_of_page[first];
    allocation_start[first] = 0;
    mark_range(first, rank, (signed char)rank);

    while (rank < MAX_RANK) {
        int size = pages_in_rank(rank);
        int buddy = first ^ size;
        if (buddy < 0 || buddy + size > page_count ||
            rank_of_page[buddy] != rank ||
            prev_free[buddy] == NONE && free_head[rank] != buddy)
            break;
        remove_free(buddy, rank);
        if (buddy < first)
            first = buddy;
        ++rank;
        mark_range(first, rank, (signed char)rank);
    }
    add_free(first, rank);
    return OK;
}

int query_ranks(void *p) {
    uintptr_t address, base, offset;
    int rank;

    if (p == NULL || pool_base == NULL)
        return -EINVAL;
    address = (uintptr_t)p;
    base = (uintptr_t)pool_base;
    if (address < base)
        return -EINVAL;
    offset = address - base;
    if (offset % PAGE_SIZE != 0 || offset / PAGE_SIZE >= (uintptr_t)page_count)
        return -EINVAL;
    rank = rank_of_page[offset / PAGE_SIZE];
    return rank < 0 ? -rank : rank;
}

int query_page_counts(int rank) {
    if (!valid_rank(rank))
        return -EINVAL;
    return free_count[rank];
}
