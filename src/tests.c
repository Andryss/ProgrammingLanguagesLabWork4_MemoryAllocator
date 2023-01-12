#define _DEFAULT_SOURCE

#include <unistd.h>


#include "tests.h"

/**
 * Prints message with heap debug
 * @param message message to print
 * @param heap heap to debug
 */
static void debug(char* message, void* heap) {
    printf("> %s\n", message);
    debug_heap(stdout, heap);
}

/**
 * Initialize heap with given size
 * @param initial_size initial size
 * @return heap pointer
 */
static void* create_heap(size_t initial_size) {
    return heap_init(initial_size);
}

/**
 * Deallocate heap
 * @param heap heap to destroy
 * @param heap_size heap size
 */
static void destroy_heap(void* heap, size_t heap_size) {
    munmap(heap, size_from_capacity((block_capacity) {.bytes = heap_size}).bytes);
}


/**
 * Test usual memory allocation and free
 */
static bool test_usual_success_alloc() {
    size_t heap_size = 4096;
    void* heap = create_heap(heap_size);
    debug("Init", heap);
    if (!heap) return false;

    void* first_alloc = _malloc(heap_size / 2);
    debug("Alloc", heap);
    if (!first_alloc) {
        destroy_heap(heap, heap_size);
        return false;
    }

    _free(first_alloc);
    debug("Free", heap);

    destroy_heap(heap, heap_size);
    return true;
}


/**
 * Test group of allocations and free in different places
 */
static bool test_single_block_free() {
    size_t heap_size = 4096;
    void* heap = create_heap(heap_size);
    debug("Init", heap);
    if (!heap) return false;

    size_t length = 10;
    size_t alloc_size = 512;
    void* allocs[length];
    for (size_t i = 0; i < length; i++) {
        allocs[i] = _malloc(alloc_size);
    }
    debug("Alloc", heap);

    for (size_t i = 0; i < length; i++) {
        if (allocs[i] == NULL) {
            destroy_heap(heap, heap_size);
            return false;
        }
    }

    _free(allocs[0]);
    _free(allocs[2]);
    _free(allocs[9]);
    debug("Free", heap);

    destroy_heap(heap, heap_size);
    return true;
}


/**
 * Test merging (group of free in one place)
 */
static bool test_double_block_free() {
    size_t heap_size = 4096;
    void* heap = create_heap(heap_size);
    debug("Init", heap);
    if (!heap) return false;

    size_t length = 10;
    size_t alloc_size = 512;
    void* allocs[length];
    for (size_t i = 0; i < length; i++) {
        allocs[i] = _malloc(alloc_size);
    }
    debug("Alloc", heap);

    for (size_t i = 0; i < length; i++) {
        if (allocs[i] == NULL) {
            destroy_heap(heap, heap_size);
            return false;
        }
    }

    _free(allocs[5]);
    _free(allocs[4]);
    _free(allocs[3]);
    debug("Free", heap);

    destroy_heap(heap, heap_size);
    return true;
}


/**
 * Test heap growing (expanding the end of heap)
 */
static bool test_grow_heap_and_merge() {
    size_t heap_size = 4096;
    void* heap = create_heap(heap_size);
    debug("Init", heap);
    if (!heap) return false;

    void* first_alloc = _malloc(heap_size * 2);
    debug("Alloc", heap);
    if (!first_alloc) {
        destroy_heap(heap, heap_size);
        return false;
    }

    struct block_header* heap_header = (struct block_header*) heap;
    if (heap_header->capacity.bytes < heap_size * 2) {
        destroy_heap(heap, size_from_capacity(heap_header->capacity).bytes);
        return false;
    }

    _free(first_alloc);
    debug("Free", heap);

    destroy_heap(heap, heap_size);
    return true;
}


/**
 * Test heap growing (but there is a barrier at the end of heap)
 */
static bool test_grow_heap_no_merge() {
    size_t heap_size = 4096;
    void* heap = create_heap(heap_size);
    debug("Init", heap);
    if (!heap) return false;

    size_t wall_size = 1024;
    void* wall = mmap(heap + heap_size, wall_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    size_t alloc_size = heap_size * 2;
    void* first_alloc = _malloc(alloc_size);
    debug("Alloc", heap);
    if (!first_alloc) {
        munmap(wall, wall_size);
        destroy_heap(heap, heap_size);
        return false;
    }

    struct block_header* heap_header = (struct block_header*) heap;
    if (!heap_header->is_free || heap_header->next->is_free) {
        munmap(wall, wall_size);
        destroy_heap(heap, size_from_capacity(heap_header->capacity).bytes);
        munmap(first_alloc, alloc_size);
        return false;
    }

    _free(first_alloc);
    debug("Free", heap);

    munmap(wall, wall_size);
    destroy_heap(heap, size_from_capacity(heap_header->capacity).bytes);
    munmap(first_alloc, alloc_size);
    return true;
}


/**
 * Array with tests
 */
test_func simple_test_funcs[] = {
        test_usual_success_alloc,
        test_single_block_free,
        test_double_block_free,
        test_grow_heap_and_merge,
        test_grow_heap_no_merge
};

/**
 * Length of simple_test_funcs
 */
size_t simple_test_funcs_count = 5;


void test_func_simple_handler(test_func test, size_t num) {
    fprintf(stdout, "\n---------------- TEST %zu ----------------\n", num);
    if (!test()) fprintf(stderr, "TEST %zu failed\n", num);
    else fprintf(stdout, "TEST %zu passed\n", num);
}


void execute_tests(test_func* tests, size_t tests_count, test_func_handler handler) {
    for (size_t i = 0; i < tests_count; i++) {
        handler(tests[i], i);
    }
}
