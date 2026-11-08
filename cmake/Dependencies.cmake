# Third-party dependencies (pinned versions), fetched at configure time.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
# Zamin is a shared library (game modules link against it), so everything must be PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- GLFW (window + input, cross-platform) --------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)

# ---- GLM (math) ------------------------------------------------------------
FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1 GIT_SHALLOW TRUE)

# ---- EnTT (ECS) ------------------------------------------------------------
FetchContent_Declare(entt GIT_REPOSITORY https://github.com/skypjack/entt.git GIT_TAG v3.13.2 GIT_SHALLOW TRUE)

# ---- nlohmann/json (serialization) ----------------------------------------
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)

# ---- Box2D (2D physics) ----------------------------------------------------
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED    OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS       OFF CACHE BOOL "" FORCE)
FetchContent_Declare(box2d GIT_REPOSITORY https://github.com/erincatto/box2d.git GIT_TAG v2.4.1 GIT_SHALLOW TRUE)

# ---- Jolt Physics (3D physics) ----------------------------------------------
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(USE_ASSERTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git GIT_TAG v5.2.0 GIT_SHALLOW TRUE SOURCE_SUBDIR Build)

FetchContent_MakeAvailable(glfw glm entt json box2d JoltPhysics)
set_target_properties(Jolt PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- Sources without (usable) CMake: populate and build our own targets ----
FetchContent_Declare(imgui    GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5-docking GIT_SHALLOW TRUE)
FetchContent_Declare(imguizmo GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git GIT_TAG b10e91756d32395f5c1fefd417899b657ed7cb88)
FetchContent_Declare(stb      GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG d14d46f6b44456eb88beac58cba833a4135d4c89)
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.21 GIT_SHALLOW TRUE)
FetchContent_Declare(cgltf GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git GIT_TAG v1.14 GIT_SHALLOW TRUE)
FetchContent_Declare(tinyobj  GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git GIT_TAG v2.0.0rc13 GIT_SHALLOW TRUE)

foreach(dep imgui imguizmo stb tinyobj miniaudio cgltf)
    FetchContent_GetProperties(${dep})
    if(NOT ${dep}_POPULATED)
        FetchContent_Populate(${dep})
    endif()
endforeach()

# glad (generated, vendored)
add_library(glad OBJECT ${PROJECT_SOURCE_DIR}/vendor/glad/src/glad.c)
target_include_directories(glad PUBLIC ${PROJECT_SOURCE_DIR}/vendor/glad/include)

# Dear ImGui (docking) + GLFW/OpenGL3 backends
add_library(imgui OBJECT
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)
# Our ImGui options (Zamin/ImGui/ImGuiConfig.h): IM_ASSERT kept in every build, a failed check logged as an
# error. Everything that includes imgui.h gets the same (Zamin's public definitions, ZaminConfig.cmake).
# Zamin/src is on ImGui's (and ImGuizmo's) own include path only - PRIVATE: as a PUBLIC directory of imgui it
# would be one of the system directories below for the engine's sources, and the warnings in the engine's own
# headers would be hidden there (our targets have Zamin/src from the Zamin target).
target_include_directories(imgui PRIVATE ${PROJECT_SOURCE_DIR}/Zamin/src)
set(ZAMIN_IMGUI_CONFIG "IMGUI_USER_CONFIG=<Zamin/ImGui/ImGuiConfig.h>")
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS ${ZAMIN_IMGUI_CONFIG})

# ImGuizmo (transform gizmos)
add_library(imguizmo OBJECT ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp)
target_include_directories(imguizmo PUBLIC ${imguizmo_SOURCE_DIR})
target_include_directories(imguizmo PRIVATE ${PROJECT_SOURCE_DIR}/Zamin/src) # ImGuiConfig.h (above)
target_link_libraries(imguizmo PUBLIC imgui)

# stb (image loading)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

# tinyobjloader (OBJ models)
add_library(tinyobj INTERFACE)
target_include_directories(tinyobj INTERFACE ${tinyobj_SOURCE_DIR})

# cgltf (glTF 2.0 models)
add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${cgltf_SOURCE_DIR})

# miniaudio (audio). SYSTEM: its implementation is compiled in AudioEngine.cpp, and its own
# warnings (e.g. Clang's -Wunused-but-set-variable in Release) are not the engine's.
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio SYSTEM INTERFACE ${miniaudio_SOURCE_DIR})

foreach(t glad imgui imguizmo)
    set_target_properties(${t} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()

# Vendor headers are system headers for the targets that use them (-isystem): their own warnings are
# not ours - our targets build with -Wall -Wextra -Wpedantic (zamin_target_settings). miniaudio above
# is SYSTEM already. Only the vendor targets' interface directories: none of them is ours (Zamin/src stays
# a plain -I directory everywhere, checked below).
foreach(t glm glm-header-only EnTT nlohmann_json box2d Jolt glfw glad imgui imguizmo stb tinyobj cgltf)
    get_target_property(_zm_inc ${t} INTERFACE_INCLUDE_DIRECTORIES)
    if(_zm_inc)
        foreach(_zm_ours Zamin Editor Runtime Tests Projects)
            string(FIND "${_zm_inc}" "${PROJECT_SOURCE_DIR}/${_zm_ours}/" _zm_at)
            if(NOT _zm_at EQUAL -1)
                message(FATAL_ERROR "${t}: ${_zm_ours}/ papkasi tizim (-isystem) papkasi bo'lib qolardi: ${_zm_inc}")
            endif()
        endforeach()
        set_target_properties(${t} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_zm_inc}")
    endif()
endforeach()
