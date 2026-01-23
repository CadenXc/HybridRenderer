#pragma once

#include <iostream>
#include <memory>
#include <utility>
#include <algorithm>
#include <functional>
#include <string>
#include <sstream>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>
#include <fstream>
#include <limits>
#include <chrono>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <cstdio>

#ifdef _WIN32
    #include <windows.h>
    #include <windowsx.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <volk.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

#include <stb_image.h>
#include "vk_mem_alloc.h" 
#include "tiny_obj_loader.h" 

#include "Core/Application.h"
#include "Core/Log.h"