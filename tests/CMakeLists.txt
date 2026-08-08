add_executable(TaskSystemTests
    TaskSystemTests.cpp
)

target_link_libraries(TaskSystemTests
    PRIVATE Chimera
)

add_test(
    NAME TaskSystemTests
    COMMAND TaskSystemTests
)

set_tests_properties(TaskSystemTests PROPERTIES
    TIMEOUT 10
)
