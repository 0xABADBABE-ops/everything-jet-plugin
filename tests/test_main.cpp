//
// test_main.cpp — Entry point for all tests.
// Includes all test files into a single translation unit for static
// registration of test functions.
//

#include "test_framework.hpp"

#include "test_json.cpp"
#include "test_string_utils.cpp"
#include "test_everything.cpp"
#include "test_content_search.cpp"
#include "test_smart_query.cpp"
#include "test_actions.cpp"
#include "test_file_hashing.cpp"
#include "test_file_preview.cpp"
#include "test_mcp_server.cpp"

TEST_MAIN()
