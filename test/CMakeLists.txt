add_library(unity STATIC ${PROJECT_SOURCE_DIR}/third_party/unity/unity.c)
target_include_directories(unity PUBLIC ${PROJECT_SOURCE_DIR}/third_party/unity)

add_library(test_support STATIC
    fakes/fake_flash.c
    support/test_support.c
)
target_include_directories(test_support PUBLIC fakes support)
target_link_libraries(test_support PUBLIC sota_core unity)
target_compile_definitions(test_support PRIVATE
    VECTOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors")
target_compile_options(test_support PRIVATE ${SOTA_WARNINGS})

foreach(name crc32 image bootstate boot update powercut proto)
    add_executable(test_${name} test_${name}.c)
    target_link_libraries(test_${name} PRIVATE test_support)
    target_compile_options(test_${name} PRIVATE ${SOTA_WARNINGS})
    add_test(NAME ${name} COMMAND test_${name})
endforeach()
