cmake_minimum_required(VERSION 3.10)
project(LLM_Circuit_UI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Include Drogon (Internal Libs)
add_subdirectory(libs/drogon)

# 2. Source Files
set(SOURCE_FILES 
    src/main.cpp
    src/GraphController.h # Drogon discovers controllers in headers
)

# 3. Create Executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# 4. Link Dependencies
# Drogon provides a target that handles JsonCpp, Trantor, and UUID automatically
target_link_libraries(${PROJECT_NAME} PRIVATE drogon)

# Ensure the compiler can see your src/ folder
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)