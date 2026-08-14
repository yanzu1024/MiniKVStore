cmake_minimum_required(VERSION 3.21)

project(MiniKVStore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINIKV_BUILD_TESTS "Build MiniKV tests" ON)
option(MINIKV_BUILD_BENCHMARKS "Build the MiniKV benchmark" ON)
option(MINIKV_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MINIKV_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)

if(MINIKV_ENABLE_SANITIZERS AND MINIKV_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "Enable either ASan/UBSan or ThreadSanitizer, not both")
endif()

find_package(Threads REQUIRED)

function(minikv_set_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(minikv_enable_sanitizers target_name)
    if(MINIKV_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
    elseif(MINIKV_ENABLE_THREAD_SANITIZER AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target_name} PRIVATE -fsanitize=thread)
    endif()
endfunction()

add_library(MiniKVCore STATIC
    src/AppendOnlyStore.cpp
    src/CommandParser.cpp
    src/KeyValueStore.cpp
    src/Record.cpp
    src/Status.cpp
)

target_include_directories(MiniKVCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(MiniKVCore PUBLIC Threads::Threads)
minikv_set_warnings(MiniKVCore)
minikv_enable_sanitizers(MiniKVCore)

add_executable(minikv app/main.cpp)
target_link_libraries(minikv PRIVATE MiniKVCore)
minikv_set_warnings(minikv)
minikv_enable_sanitizers(minikv)

if(MINIKV_BUILD_TESTS)
    enable_testing()

    add_executable(MiniKVTests
        tests/test_main.cpp
        tests/test_command_parser.cpp
        tests/test_concurrency.cpp
        tests/test_expiration.cpp
        tests/test_lru_cache.cpp
        tests/test_persistence.cpp
    )
    target_link_libraries(MiniKVTests PRIVATE MiniKVCore)
    target_include_directories(MiniKVTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    minikv_set_warnings(MiniKVTests)
    minikv_enable_sanitizers(MiniKVTests)

    add_test(NAME MiniKVTests COMMAND MiniKVTests)
endif()

if(MINIKV_BUILD_BENCHMARKS)
    add_executable(MiniKVBenchmark benchmarks/benchmark_store.cpp)
    target_link_libraries(MiniKVBenchmark PRIVATE MiniKVCore)
    minikv_set_warnings(MiniKVBenchmark)
    minikv_enable_sanitizers(MiniKVBenchmark)
endif()

install(TARGETS minikv RUNTIME DESTINATION bin)
