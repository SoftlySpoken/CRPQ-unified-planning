cmake_minimum_required(VERSION 3.12)

# Set project name and version
project(MillenniumDB VERSION 0.2.1)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Enable export of compile commands output
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use all cores but one for compilation
include(ProcessorCount)
ProcessorCount(NUM_CORES)
if(NUM_CORES GREATER 1)
    math(EXPR PARALLEL_LEVEL "${NUM_CORES} - 1")
    set(CMAKE_BUILD_PARALLEL_LEVEL ${PARALLEL_LEVEL})
endif()

# Set output directories
set(EXECUTABLE_OUTPUT_PATH bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY lib)

if(APPLE)
    message("https://cmake.org/cmake/help/latest/variable/APPLE.html?highlight=apple")

    set(BREW_DIRS "/opt/homebrew/opt" "/usr/local/opt")

    foreach(BREW_DIR ${BREW_DIRS})
        if(EXISTS ${BREW_DIR})
            include_directories("${BREW_DIR}/openssl/include")
            include_directories("${BREW_DIR}/ncurses/include")
            link_directories("${BREW_DIR}/openssl@3/lib")
        endif(EXISTS ${BREW_DIR})
    endforeach(BREW_DIR)

    set(NCURSES ncurses)
    set(FS "")
else()
    set(NCURSES ncursesw)
    set(FS "stdc++fs")
endif(APPLE)

# Define the compiler flags
set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic -pthread -march=native -funroll-loops -fno-operator-names")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g3 -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -g3 -fno-omit-frame-pointer")

# Define SANITIZE build type for AddressSanitizer and UndefinedBehaviorSanitizer
set(CMAKE_CXX_FLAGS_SANITIZE "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined -fno-sanitize-recover=all -fstack-protector-strong")
set(CMAKE_EXE_LINKER_FLAGS_SANITIZE "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")
set(CMAKE_SHARED_LINKER_FLAGS_SANITIZE "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")

if(PROFILE)
    add_definitions(-DBOOST_STACKTRACE_USE_ADDR2LINE)
endif(PROFILE)


# When compiling with Clang generate debug info specifically for lldb
if(CMAKE_CXX_COMPILER_ID STREQUAL Clang)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -glldb -fstandalone-debug")
endif()

if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated")
endif(APPLE)

# Enable OpenMP if available
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
else()
    message(WARNING "OpenMP not found")
endif(OpenMP_CXX_FOUND)

# Enable interprocedural optimization if supported and BUILD_TYPE is RELEASE
string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE)
# if(BUILD_TYPE STREQUAL "RELEASE")
#     include(CheckIPOSupported)
#     check_ipo_supported(RESULT ipo_result OUTPUT ipo_output)
#     if(ipo_result)
#         set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
#     else()
#         message(WARNING "IPO is not supported:\n${ipo_output}")
#     endif(ipo_result)
# endif(BUILD_TYPE STREQUAL "RELEASE")

# Add include directories
include_directories(${CMAKE_SOURCE_DIR}/src)

# Define sources
file(GLOB_RECURSE SRCS src/*.cc)
file(GLOB_RECURSE BIN_SRCS src/bin/*.cc)
file(GLOB_RECURSE TEST_SRCS src/tests/*.cc)
file(GLOB_RECURSE API_SRCS src/api/*.cc)
# Remove unwanted files from common sources
list(REMOVE_ITEM SRCS ${BIN_SRCS})
list(REMOVE_ITEM SRCS ${TEST_SRCS})

message(STATUS "Build MillenniumDB's binaries")
# Third party dependencies
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime-4.13.1)
include_directories(${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime-4.13.1/src)
include_directories(${CMAKE_SOURCE_DIR}/third_party/boost_1_81/include)
# Define targets
add_library(millenniumdb STATIC ${SRCS})
target_compile_definitions(millenniumdb PUBLIC SERD_STATIC)
set(BUILD_TARGETS
    mdb-server
    mdb-cli
    mdb-dump
    mdb-import
    mdb-import-tensors
    mdb-index-tensors
    # mdb-scsu
    # mdb-text-search
)
set(PROFILE_TARGETS
    profile-query
)
set(TEST_TARGETS
    compare_datetime
    compare_decimal_both_ext
    compare_decimal_both_inl
    compare_decimal_inl_ext
    decimal_operations
    iri_prefixes-test
    normalize_decimal
    regular_path_expr_to_rpq_dfa
    scsu-test
    variable_set
    union_operator-test
    sj_operator-test
    ti_operator-test
    ti_two_way_operator-test
    kc_operator-test
    mc_operator-test
    two_column_store-test
    bplus_tree_two_column_binding_iter-test
    custom_ops_bplus_tree_integration-test
    custom_planner-test
    constant_col2_features-test
)
# Build targets
foreach(target ${BUILD_TARGETS})
    add_executable(${target} ${CMAKE_SOURCE_DIR}/src/bin/${target}.cc)
    target_link_libraries(${target}
        millenniumdb
        antlr4_cpp_runtime
        ${FS}
        ssl
        crypto
        ${NCURSES}
    )
endforeach(target)

# Test targets
enable_testing()
foreach(target ${TEST_TARGETS})
    add_executable(${target} ${CMAKE_SOURCE_DIR}/src/tests/${target}.cc)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    target_link_libraries(${target}
        millenniumdb
        antlr4_cpp_runtime
        ${FS}
        ssl
        crypto
    )
    add_test(
        NAME ${target}
        COMMAND $<TARGET_FILE:${target}>
        WORKING_DIRECTORY tests
    )
endforeach(target)

if(PROFILE)
    # Profile targets
    foreach(target ${PROFILE_TARGETS})
        add_executable(${target} ${CMAKE_SOURCE_DIR}/src/bin/${target}.cc)
        target_link_libraries(${target}
            millenniumdb
            antlr4_cpp_runtime
            ${FS}
            ssl
            crypto
            dl
            -Wl,-no-as-needed
            -no-pie
            -fno-pie
            tcmalloc
            profiler
        )
    endforeach(target)
endif(PROFILE)

install(TARGETS ${BUILD_TARGETS} RUNTIME DESTINATION bin)
