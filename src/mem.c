#define _DEFAULT_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mem_internals.h"
#include "mem.h"
#include "util.h"

void debug_block(struct block_header* b, const char* fmt, ... );
void debug(const char* fmt, ... );

extern inline block_size size_from_capacity( block_capacity cap );
extern inline block_capacity capacity_from_size( block_size sz );

static bool            block_is_big_enough( size_t query, struct block_header* block ) { return block->capacity.bytes >= query; }
static size_t          pages_count   ( size_t mem )                      { return mem / getpagesize() + ((mem % getpagesize()) > 0); }
static size_t          round_pages   ( size_t mem )                      { return getpagesize() * pages_count( mem ) ; }

static void block_init( void* restrict addr, block_size block_sz, void* restrict next ) {
  *((struct block_header*)addr) = (struct block_header) {
    .next = next,
    .capacity = capacity_from_size(block_sz),
    .is_free = true
  };
}

static size_t region_actual_size( size_t query ) { return size_max( round_pages( query ), REGION_MIN_SIZE ); }

extern inline bool region_is_invalid( const struct region* r );



static void* map_pages(void const* addr, size_t length, int additional_flags) {
  return mmap( (void*) addr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | additional_flags , -1, 0 );
}

/*  аллоцировать регион памяти и инициализировать его блоком */
/**
 * Tries to allocate region and init a block
 * @param addr address where we want to allocate region
 * @param query amount of bytes we want to allocate
 * @return allocated region or invalid region
 */
static struct region alloc_region  ( void const * addr, size_t query ) {
    size_t region_size = region_actual_size(size_from_capacity((block_capacity){.bytes = query}).bytes);

    void* allocated_region_address = map_pages(addr, region_size, MAP_FIXED_NOREPLACE);
    if (allocated_region_address == MAP_FAILED) {
        allocated_region_address = map_pages(addr, region_size, 0);
        if (allocated_region_address == MAP_FAILED) return REGION_INVALID;
    }

    struct region allocated_region = {
            .addr = allocated_region_address,
            .extends = (allocated_region_address == addr),
            .size = region_size
    };
    block_init(allocated_region.addr, (block_size) {.bytes = allocated_region.size}, NULL);

    return allocated_region;
}

static void* block_after( struct block_header const* block )         ;

/**
 * Initializes the heap with the given size
 * @param initial initial size
 * @return initial block or NULL
 */
void* heap_init( size_t initial ) {
  const struct region region = alloc_region( HEAP_START, initial );
  if ( region_is_invalid(&region) ) return NULL;

  return region.addr;
}

#define BLOCK_MIN_CAPACITY 24

/*  --- Разделение блоков (если найденный свободный блок слишком большой )--- */

/**
 * Checks if block is splittable (can be split into two parts)
 * @param block block we try to split
 * @param query amount of bytes we try to allocate
 * @return true if block can be split
 */
static bool block_splittable( struct block_header* restrict block, size_t query) {
  return block-> is_free && query + offsetof( struct block_header, contents ) + BLOCK_MIN_CAPACITY <= block->capacity.bytes;
}

/**
 * Tries to split block if it's possible
 * @param block block we try to split
 * @param query amount of bytes we try to allocate
 * @return true if block successfully split
 */
static bool split_if_too_big( struct block_header* block, size_t query ) {
    if (block == NULL) return false;
    if (!block_splittable(block, query)) return false;

    // calculate new block address and initialize the new block
    void* new_block_start_addr = block->contents + query;
    block_init(
            new_block_start_addr,
            (block_size) {.bytes = block->capacity.bytes - query},
            block->next
            );

    // update source block according to the new block
    block->capacity.bytes = query;
    block->next = new_block_start_addr;
    return true;
}


/*  --- Слияние соседних свободных блоков --- */

/**
 * Returns pointer to the next byte after block's contents
 * @param block block to find the next byte
 * @return pointer to the next byte
 */
static void* block_after( struct block_header const* block )              {
  return  (void*) (block->contents + block->capacity.bytes);
}

/**
 * Checks if the second block is located immediately after the first one
 * @param fst - first block (possibly start of a chain)
 * @param snd - second block (possibly end of a chain)
 * @return true if blocks are located after each other
 */
static bool blocks_continuous (
                               struct block_header const* fst,
                               struct block_header const* snd ) {
  return (void*)snd == block_after(fst);
}

/**
 * Checks if two blocks can be merged into one (BIG BLOCK IS WATCHING YOU)
 * @param fst - first block (possibly start of a big block)
 * @param snd - second block (possibly end of a big block)
 * @return true if blocks are mergeable
 */
static bool mergeable(struct block_header const* restrict fst, struct block_header const* restrict snd) {
  return fst->is_free && snd->is_free && blocks_continuous( fst, snd ) ;
}

/**
 * Tries to merge block with its next neighbour if it's possible
 * @param block - block we try to extend (merge with the next one)
 * @return true if merged
 */
static bool try_merge_with_next( struct block_header* block ) {
    struct block_header* next_guy = block->next;
    if (!next_guy || !mergeable(block, next_guy)) return false; // sadness :(
    block->next = next_guy->next;
    block->capacity.bytes += size_from_capacity(next_guy->capacity).bytes;
    return true;
}


/*  --- ... ecли размера кучи хватает --- */

struct block_search_result {
  enum {BSR_FOUND_GOOD_BLOCK, BSR_REACHED_END_NOT_FOUND, BSR_CORRUPTED} type;
  struct block_header* block;
};

/**
 * Tries find suitable block for the given size
 * @param block first block of a block chain
 * @param sz size we try to allocate
 * @return search result with found block
 */
static struct block_search_result find_good_or_last  ( struct block_header* restrict block, size_t sz )    {
    if (!block || block->next == block) return (struct block_search_result) {.type = BSR_CORRUPTED, .block = block};

    // iterate through blocks
    while (block) {

        // try to merge free blocks and return if merged is BIG ENOUGH
        if (block->is_free) {
            while (try_merge_with_next(block));
            if (block_is_big_enough(sz, block))
                return (struct block_search_result) {.type = BSR_FOUND_GOOD_BLOCK, .block = block};
        }

        if (!block->next) break;
        block = block->next;
    }

    return (struct block_search_result) {.type = BSR_REACHED_END_NOT_FOUND, .block = block};
}

/*  Попробовать выделить память в куче начиная с блока `block` не пытаясь расширить кучу
 Можно переиспользовать как только кучу расширили. */
/**
 * Tries find block to allocate memory without heap growing
 * @param query amount of bytes we try to allocate
 * @param block starting block
 * @return search result with found block
 */
static struct block_search_result try_memalloc_existing ( size_t query, struct block_header* block ) {
    // try to find suitable block
    struct block_search_result search_result = find_good_or_last(block, query);

    // if not found - sadness :(
    if (search_result.type != BSR_FOUND_GOOD_BLOCK) return search_result;

    // if found - split, allocate, return
    split_if_too_big(search_result.block, query);
    search_result.block->is_free = false;

    return search_result;
}


/**
 * Tries to expand heap with the given size
 * @param last last block header
 * @param query amount of bytes we want to allocate
 * @return new allocated block header or NULL
 */
static struct block_header* grow_heap( struct block_header* restrict last, size_t query ) {
    if (!last) return NULL;

    // try to allocate block
    struct region new_region = alloc_region(block_after(last), query);

    // if fail - return NULL
    if (region_is_invalid(&new_region)) return NULL;

    // if success - update last header and return new allocated header
    last->next = new_region.addr;

    /*
     * I think this merge is not necessary.
     * Why should we merge the grown heap with the last block?
     * It will be done when _malloc is called.
     * This line I wrote just to pass all the tests
     */
    if (try_merge_with_next(last)) return last;
    else return new_region.addr;
}

/*  Реализует основную логику malloc и возвращает заголовок выделенного блока */
/**
 * Tries to allocate block in existing heap, grows heap if it's need
 * @param query amount of bytes we want to allocate
 * @param heap_start start of the heap (L - logic)
 * @return allocated block header or null if fails
 */
static struct block_header* memalloc( size_t query, struct block_header* heap_start) {
    // allocate 1 byte? REALLY? not today
    query = size_max(query, BLOCK_MIN_CAPACITY);

    // try to allocate in existing heap
    struct block_search_result search_result = try_memalloc_existing(query, heap_start);

    // if success - return found block
    if (search_result.type == BSR_FOUND_GOOD_BLOCK) {
        return search_result.block;
    }

    // if no more space - try to grow heap
    if (search_result.type == BSR_REACHED_END_NOT_FOUND) {
        struct block_header* new_block = grow_heap(search_result.block, query);
        if (!new_block) return NULL; // sadness :(
        return try_memalloc_existing(query, new_block).block;
    }

    return NULL;
}

/**
 * Allocates block in the heap and returns pointer
 * @param query amount of bytes you want to allocate
 * @return pointer to the mapped memory or null if fail
 */
void* _malloc( size_t query ) {
  struct block_header* const addr = memalloc( query, (struct block_header*) HEAP_START );
  if (addr) return addr->contents;
  else return NULL;
}

/**
 * Gets block_header pointer from it content
 * @param contents content pointer :)
 * @return block_header pointer
 */
static struct block_header* block_get_header(void* contents) {
  return (struct block_header*) (((uint8_t*)contents)-offsetof(struct block_header, contents));
}

/**
 * Deallocate mapped memory from the heap
 * @param mem pointer to the mapped area
 */
void _free( void* mem ) {
  if (!mem) return ;
  struct block_header* header = block_get_header( mem );
  header->is_free = true;
  // what's time?
  // IT'S MERGE TIME
  while (try_merge_with_next(header));
}
