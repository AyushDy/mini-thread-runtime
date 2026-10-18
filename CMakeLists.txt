cmake_minimum_required(VERSION 3.20)

project(mini_thread_runtime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(mini_thread_runtime
    src/context.cpp
    src/runtime.cpp
)

target_include_directories(mini_thread_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(mini_thread_runtime PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(BUILD_TESTING)
    add_executable(mini_thread_smoke_test
        tests/smoke_test.cpp
    )

    target_compile_options(mini_thread_smoke_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME mini_thread_smoke_test COMMAND mini_thread_smoke_test)

    add_executable(mini_thread_context_test
        tests/context_test.cpp
    )

    target_link_libraries(mini_thread_context_test PRIVATE
        mini_thread_runtime
    )

    target_compile_options(mini_thread_context_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME mini_thread_context_test COMMAND mini_thread_context_test)

    add_executable(mini_thread_runtime_test
        tests/runtime_test.cpp
    )

    target_link_libraries(mini_thread_runtime_test PRIVATE
        mini_thread_runtime
    )

    target_compile_options(mini_thread_runtime_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_test(NAME mini_thread_runtime_test COMMAND mini_thread_runtime_test)

    add_executable(producer_consumer
        examples/producer_consumer.cpp
    )

    target_link_libraries(producer_consumer PRIVATE
        mini_thread_runtime
    )

    target_compile_options(producer_consumer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
