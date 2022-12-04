#ifndef TESTS_H_
#define TESTS_H_

#include "mem.h"
#include "mem_internals.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <sys/mman.h>

/**
 * Definition of test function
 * @return true if test was successful, false otherwise
 */
typedef bool (* test_func)(void);
/**
 * Simple implementation of test functions
 */
extern test_func simple_test_funcs[];
/**
 * Just length of simple_test_funcs
 */
extern size_t simple_test_funcs_count;

/**
 * Definition of test function handler
 * @param test test function to execute
 * @param num number of test
 */
typedef void (* test_func_handler)(test_func test, size_t num);
/**
 * Simple test handler implementation
 */
void test_func_simple_handler(test_func test, size_t num);

/**
 * Main function, which handle tests
 * @param tests test functions
 * @param tests_count tests array length
 * @param handler test functions handler
 */
void execute_tests(test_func* tests, size_t tests_count, test_func_handler handler);

#endif //TESTS_H_
