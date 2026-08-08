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

add_executable(RenderGraphTests
    RenderGraphTests.cpp
)

target_link_libraries(RenderGraphTests
    PRIVATE Chimera
)

add_test(
    NAME RenderGraphTests
    COMMAND RenderGraphTests
)

set_tests_properties(RenderGraphTests PROPERTIES
    TIMEOUT 10
)

add_executable(ShaderAbiTests
    ShaderAbiTests.cpp
)

target_link_libraries(ShaderAbiTests
    PRIVATE Chimera
)

add_test(
    NAME ShaderAbiTests
    COMMAND ShaderAbiTests
)