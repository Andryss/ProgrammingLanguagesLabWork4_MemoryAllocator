#ifndef TESTS_H_
#define TESTS_H_

#include "mem.h"
#include "mem_internals.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <sys/mman.h>


typedef bool (* test_func)(void);
extern test_func simple_test_funcs[];

typedef void (* test_func_handler)(test_func, size_t num);
void test_func_simple_handler(test_func test, size_t num);

void execute_tests(test_func_handler handler);

#endif //TESTS_H_
