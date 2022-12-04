#include "tests.h"

int main() {
    // Let's test some tests
    execute_tests(
            simple_test_funcs,
            simple_test_funcs_count,
            test_func_simple_handler
            );
}