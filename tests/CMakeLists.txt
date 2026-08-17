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

add_executable(BenchmarkRecorderTests
    BenchmarkRecorderTests.cpp
)

target_link_libraries(BenchmarkRecorderTests
    PRIVATE Chimera
)

add_test(
    NAME BenchmarkRecorderTests
    COMMAND BenchmarkRecorderTests
)

set_tests_properties(BenchmarkRecorderTests PROPERTIES
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

target_compile_definitions(ShaderAbiTests
    PRIVATE CHIMERA_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/Chimera/shaders"
)

add_test(
    NAME ShaderAbiTests
    COMMAND ShaderAbiTests
)

add_executable(ImageComparisonTests
    ImageComparisonTests.cpp
)

target_link_libraries(ImageComparisonTests
    PRIVATE Chimera
)

add_test(
    NAME ImageComparisonTests
    COMMAND ImageComparisonTests
)

set_tests_properties(ImageComparisonTests PROPERTIES
    TIMEOUT 10
)

add_executable(ResourceIdentityTests
    ResourceIdentityTests.cpp
)

target_link_libraries(ResourceIdentityTests
    PRIVATE Chimera
)

add_test(
    NAME ResourceIdentityTests
    COMMAND ResourceIdentityTests
)

set_tests_properties(ResourceIdentityTests PROPERTIES
    TIMEOUT 10
)
