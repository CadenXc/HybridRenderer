#include "Scene/EditorCamera.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void TestFrameBoundsCentersAndFitsScene()
{
    Chimera::EditorCamera camera(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.SetViewportSize(1600.0f, 900.0f);

    const Chimera::ChimeraAABB bounds({-1.0f, -2.0f, -0.5f},
                                      {3.0f, 2.0f, 0.5f});
    camera.FrameBounds(bounds);

    const glm::vec3 expectedCenter(1.0f, 0.0f, 0.0f);
    Require(glm::length(camera.GetFocalPoint() - expectedCenter) < 0.0001f,
            "framed camera must look at the bounds center");
    Require(camera.GetDistance() > glm::length(bounds.GetExtent()),
            "framed camera distance must contain the bounding sphere");
    Require(camera.IsUpdated(),
            "framing a scene must mark the camera as updated");
}
} // namespace

int main()
{
    try
    {
        TestFrameBoundsCentersAndFitsScene();
        std::cout << "[PASS] camera framing centers and fits scene bounds\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
