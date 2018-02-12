/*
 * Created by Ivo Georgiev on 2/9/16.
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h> // for perror()

#include "mem_pool.h"

/*************/
/*           */
/* Constants */
/*           */
/*************/
static const float      MEM_FILL_FACTOR                 = 0.75;
static const unsigned   MEM_EXPAND_FACTOR               = 2;

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = 0.75;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = 2;



/*********************/
/*                   */
/* Type declarations */
/*                   */
/*********************/
typedef struct _alloc {
    char *mem;
    size_t size;
} alloc_t, *alloc_pt;

typedef struct _node {
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev; // doubly-linked list for gap deletion
} node_t, *node_pt;

typedef struct _gap {
    size_t size;
    node_pt node;
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;
} pool_mgr_t, *pool_mgr_pt;



/***************************/
/*                         */
/* Static global variables */
/*                         */
/***************************/
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;



/********************************************/
/*                                          */
/* Forward declarations of static functions */
/*                                          */
/********************************************/
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status
        _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                           size_t size,
                           node_pt node);
static alloc_status
        _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                size_t size,
                                node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status _mem_invalidate_gap_ix(pool_mgr_pt pool_mgr);



/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/
alloc_status mem_init() {
        // allocate the pool store with initial capacity
    if (pool_store == NULL)
    {
        pool_store = (pool_mgr_pt*)calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_pt));
        pool_store_size = 0;
        pool_store_capacity = MEM_POOL_STORE_INIT_CAPACITY;
        return ALLOC_OK;
    }
    // ensure that it's called only once until mem_free
    else if(pool_store != NULL)
    {
        return ALLOC_CALLED_AGAIN;
    }
    else
    {
        return ALLOC_FAIL;
    }
}

alloc_status mem_free() {
    // ensure that it's called only once for each mem_init
    if (pool_store == NULL) {
        return ALLOC_CALLED_AGAIN;
    }
    // make sure all pool managers have been deallocated
    for (int i = 0; i < pool_store_capacity; i++) {
        if (pool_store[i] != NULL) {
            return ALLOC_FAIL;
        }
    }
    // can free the pool store array
    free(pool_store);
    // update static variables
    pool_store = NULL;
    pool_store_capacity = 0;
    pool_store_size = 0;
    return ALLOC_OK;
}

pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    // make sure there the pool store is allocated
    if(pool_store == NULL )
    {
        return NULL;
    }
    // expand the pool store, if necessary
    // allocate a new mem pool mgr
    pool_store[pool_store_size] = (pool_mgr_t*) calloc(1, sizeof(pool_mgr_t));
    pool_store[pool_store_size]->pool.mem = calloc(1, size);
    // check success, on error return null
    if(pool_store[pool_store_size]->pool.mem == NULL)
    {
        return NULL;
    }
    // allocate a new memory pool

    // check success, on error deallocate mgr and return null

    // allocate a new node heap
    pool_store[pool_store_size]->node_heap = calloc(1, sizeof(node_t));
    // check success, on error deallocate mgr/pool and return null
    if(pool_store[pool_store_size]->node_heap == NULL)
    {
        free(pool_store[pool_store_size]->pool.mem);
        return NULL;
    }
    // allocate a new gap index
    pool_store[pool_store_size]->gap_ix = calloc(1, sizeof(gap_t));
    // check success, on error deallocate mgr/pool/heap and return null
    if(pool_store[pool_store_size]->gap_ix == NULL)
    {
        free(pool_store[0]->pool.mem);
        free(pool_store[0]->node_heap);
        return NULL;
    }

    // assign all the pointers and update meta data:
    //   initialize top node of node heap
    pool_store[pool_store_size]->node_heap->alloc_record.mem = pool_store[pool_store_size]->pool.mem;
    //   initialize top node of gap index
    pool_store[pool_store_size]->gap_ix->node = pool_store[pool_store_size]->node_heap;
    pool_store[pool_store_size]->gap_ix->node->alloc_record.size = size;

    //   initialize pool mgr
    //   link pool mgr to pool store

    pool_store[pool_store_size]->pool.policy = policy;
    pool_store[pool_store_size]->pool.total_size = size;
    pool_store[pool_store_size]->pool.alloc_size = 0;
    pool_store[pool_store_size]->pool.num_allocs = 0;
    pool_store[pool_store_size]->pool.num_gaps = 1;

    // return the address of the mgr, cast to (pool_pt)
    pool_mgr_pt newpool = pool_store[pool_store_size];
    pool_store_size++;

    return (pool_pt) newpool;
}

alloc_status mem_pool_close(pool_pt pool) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mangr = (pool_mgr_pt) pool;
    // check if this pool is allocated
    if(mangr == NULL)
    {
        return ALLOC_NOT_FREED;
    }
    // check if pool has only one gap
    if(mangr->pool.num_gaps > 1)
    {
        return ALLOC_NOT_FREED;
    }
    // check if it has zero allocations
    if(mangr->pool.num_allocs > 0)
    {
        return ALLOC_NOT_FREED;
    }
    // free memory pool
    free(pool->mem);
    // free node heap
    free(mangr->node_heap);
    // free gap index
    free(mangr->gap_ix);
    // find mgr in pool store and set to null
    for(int i = 0; i < pool_store_capacity; i++)
    {
        if(pool_store[i] == mangr) {
            pool_store[i] = NULL;
            break;
        }
    }
    // note: don't decrement pool_store_size, because it only grows

    // free mgr
    free(mangr);

    return ALLOC_OK;
}

void * mem_new_alloc(pool_pt pool, size_t size) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mangr = (pool_mgr_pt) pool;

    // check if any gaps, return null if none
    if(mangr->pool.num_gaps == 0)
    {
        return NULL;
    }

    // expand heap node, if necessary, quit on error
    _mem_resize_node_heap(mangr);

    // check used nodes fewer than total nodes, quit on error
    if(mangr->node_heap->used <= mangr->total_nodes)
    {
        return NULL;
    }

    // get a node for allocation:
    node_pt newNode = NULL;

    // if FIRST_FIT, then find the first sufficient node in the node heap
    if(mangr->pool.policy == FIRST_FIT)
    {
        newNode = mangr->node_heap;
        while(newNode->alloc_record.size >= size || newNode->allocated == 1)
        {
            newNode = newNode->next;
        }

    }

    // if BEST_FIT, then find the first sufficient node in the gap index
    else if(mangr->pool.policy == BEST_FIT)
    {
        for (int i = 0; i < mangr->gap_ix_capacity; ++i) {
            if(mangr->gap_ix[i].size >= size)
            {
                newNode = mangr->gap_ix[i].node;
                break;
            }
        }
    }

    // check if node found
    if(newNode->alloc_record.size < size)
    {
        return NULL;
    }

    // update metadata (num_allocs, alloc_size)
    mangr->pool.num_allocs++;
    mangr->pool.alloc_size += size;

    // calculate the size of the remaining gap, if any
    size_t reman_gap = newNode->alloc_record.size - size;

    // remove node from gap index
    _mem_remove_from_gap_ix(mangr, size, newNode);

    // convert gap_node to an allocation node of given size
    mangr->pool.num_allocs++;
    mangr->pool.alloc_size += size;

    // adjust node heap:
    //   if remaining gap, need a new node
    if(reman_gap != 0) {
        node_pt node = NULL;
        //   find an unused one in the node heap
        for(int i = 0; i < mangr->total_nodes; i++) {
            if (mangr->node_heap[i].used == 0) {
                node = &mangr->node_heap[i];
                break;
            }
        }
        //   make sure one was found
        if(node == NULL)
        {
            return NULL;
        }

        //   initialize it to a gap node
        node->alloc_record.size = reman_gap;
        node->alloc_record.mem = newNode->alloc_record.mem + size;
        node->used = 1;

        //   update metadata (used_nodes)
        mangr->used_nodes++;
        //   update linked list (new node right after the node for allocation)
        node->prev = newNode;
        node->next = newNode->next;

        //   add to gap index
        _mem_add_to_gap_ix(mangr, reman_gap, node);

        //   check if successful
        if(mangr->gap_ix != NULL)
        {
            return NULL;
        }
    }
    // return allocation record by casting the node to (alloc_pt)

    return (alloc_pt)newNode;
}

alloc_status mem_del_alloc(pool_pt pool, void * alloc) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mangr = (pool_mgr_pt) pool;

    // get node from alloc by casting the pointer to (node_pt)
    node_pt newNode = (node_pt) alloc;

    // find the node in the node heap
    int i = 0;
    while(&mangr->node_heap[i] != newNode)
    {
        ++i;
    }

    // this is node-to-delete
    // make sure it's found

    if(mangr->node_heap[i].alloc_record.size != newNode->alloc_record.size)
    {
        return ALLOC_NOT_FREED;
    }
    // convert to gap node
    newNode->allocated = 0;

    // update metadata (num_allocs, alloc_size)
    mangr->pool.num_allocs --;
    mangr->pool.alloc_size -= newNode->alloc_record.size;
    // if the next node in the list is also a gap, merge into node-to-delete
    if(newNode->next == NULL)
    {
        //   remove the next node from gap index
        _mem_remove_from_gap_ix(mangr, newNode->next->alloc_record.size, newNode->next);
        //   check success

        //   add the size to the node-to-delete


        //   update node as unused
        newNode->next->used = 0;
        newNode->next->alloc_record.size = 0;
        newNode->next->alloc_record.mem = 0;

        //   update metadata (used nodes)
        mangr->used_nodes --;
        //   update linked list:

        if (newNode->next->next != NULL) {
            newNode->next->next->prev = newNode;
            newNode->next = newNode->next->next;
        }
        else {
            newNode->next = NULL;
        }
        newNode->next->next = NULL;
        newNode->next->prev = NULL;
    }
    // this merged node-to-delete might need to be added to the gap index
    // but one more thing to check...
    // if the previous node in the list is also a gap, merge into previous!
    if(newNode->prev->allocated == 0 )
    {
        node_pt prev_n = newNode->prev;
        //   remove the previous node from gap index
        _mem_remove_from_gap_ix(mangr, prev_n->alloc_record.size, prev_n);

        //   check success


        //   add the size of node-to-delete to the previous
        prev_n->alloc_record.size += newNode->alloc_record.size;

        //   update node-to-delete as unused
        newNode->used = 0;
        newNode->alloc_record.size = 0;
        newNode->alloc_record.mem = NULL;

        //   update metadata (used_nodes)
        mangr->used_nodes --;

        //   update linked list

        if (newNode->next == NULL) {
            prev_n->next = newNode->next;
            newNode->next->prev = prev_n;
        }
        else
        {
            prev_n->next = NULL;
        }
        newNode->next = NULL;
        newNode->prev = NULL;

        //   change the node to add to the previous node!
        newNode = prev_n;
    }
    // add the resulting node to the gap index

    // check success

    return ALLOC_OK;
}

void mem_inspect_pool(pool_pt pool,
                      pool_segment_pt *segments,
                      unsigned *num_segments) {
    // get the mgr from the pool
    pool_mgr_pt  mangr = (pool_mgr_pt) pool;

    // allocate the segments array with size == used_nodes
    pool_segment_pt segment = (pool_segment_pt) calloc(mangr->used_nodes, sizeof(pool_segment_t));

    // check successful
    if(segment == NULL)
    {
        return;
    }

    // loop through the node heap and the segments array
    //    for each node, write the size and allocated in the segment
    // "return" the values:

    node_pt node = mangr->node_heap;
    int i = 0;
    while (node != NULL)
    {
        segment[i].size = node->alloc_record.size;
        segment[i].allocated = node->allocated;
        node = node->next;
        i++;
    }
    /*
                    *segments = segs;
                    *num_segments = pool_mgr->used_nodes;
     */
    *segments = segment;
    *num_segments = mangr->used_nodes;
}



/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/
static alloc_status _mem_resize_pool_store() {
    // check if necessary
    if (((float) pool_store_size / pool_store_capacity)
                    > MEM_POOL_STORE_FILL_FACTOR) {
        unsigned a = MEM_POOL_STORE_EXPAND_FACTOR*pool_store_capacity;
        pool_store = (pool_mgr_pt*) realloc(pool_store, a* sizeof(pool_mgr_pt));
        pool_store_capacity = a;
    }
    // don't forget to update capacity variables

    return ALLOC_FAIL;
}

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {
    // see above

    return ALLOC_FAIL;
}

static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
    // see above

    return ALLOC_FAIL;
}

static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                                       size_t size,
                                       node_pt node) {

    // expand the gap index, if necessary (call the function)
    // add the entry at the end
    // update metadata (num_gaps)
    // sort the gap index (call the function)
    // check success

    return ALLOC_FAIL;
}

static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                            size_t size,
                                            node_pt node) {
    // find the position of the node in the gap index
    // loop from there to the end of the array:
    //    pull the entries (i.e. copy over) one position up
    //    this effectively deletes the chosen node
    // update metadata (num_gaps)
    // zero out the element at position num_gaps!

    return ALLOC_FAIL;
}

// note: only called by _mem_add_to_gap_ix, which appends a single entry
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {
    // the new entry is at the end, so "bubble it up"
    // loop from num_gaps - 1 until but not including 0:
    //    if the size of the current entry is less than the previous (u - 1)
    //    or if the sizes are the same but the current entry points to a
    //    node with a lower address of pool allocation address (mem)
    //       swap them (by copying) (remember to use a temporary variable)

    return ALLOC_FAIL;
}

static alloc_status _mem_invalidate_gap_ix(pool_mgr_pt pool_mgr) {
    return ALLOC_FAIL;
}

