cmake_minimum_required(VERSION 3.10)
project(LLM_Circuit_UI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Include Drogon (Internal Libs)
add_subdirectory(libs/drogon)

# 2. Include llama.cpp (CPU backend by default)
# For Vulkan GPU: run cmake with -DGGML_VULKAN=ON
# For CUDA GPU: run cmake with -DGGML_CUDA=ON
set(GGML_VULKAN OFF CACHE BOOL "Enable Vulkan GPU backend")
add_subdirectory(libs/llama.cpp)

# 3. Source Files
set(SOURCE_FILES 
    src/main.cpp
    src/GraphController.h
)

# 4. Create Executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# 5. Link Dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE drogon llama)

# 6. Include Paths
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ExecuteEngine
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/llama.cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/llama.cpp/ggml/include
)
