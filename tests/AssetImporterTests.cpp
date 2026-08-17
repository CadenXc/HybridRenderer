#include "Assets/AssetImporter.h"

#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void TestTangentHandednessPreservesBitangentDirection()
{
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    const glm::vec3 tangent(1.0f, 0.0f, 0.0f);

    Require(Chimera::CalculateTangentHandedness(
                normal, tangent, glm::vec3(0.0f, 1.0f, 0.0f)) == 1.0f,
            "ordinary UV orientation must have positive tangent handedness");
    Require(Chimera::CalculateTangentHandedness(
                normal, tangent, glm::vec3(0.0f, -1.0f, 0.0f)) == -1.0f,
            "mirrored UV orientation must have negative tangent handedness");
}
} // namespace

int main()
{
    try
    {
        TestTangentHandednessPreservesBitangentDirection();
        std::cout << "[PASS] tangent handedness preserves imported bitangent direction\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
