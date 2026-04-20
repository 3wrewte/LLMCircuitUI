cmake_minimum_required(VERSION 3.10)
project(LLM_Circuit_UI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Include Drogon (Internal Libs)
add_subdirectory(libs/drogon)

# 2. Find libcurl
find_package(CURL REQUIRED)

# 3. Source Files
set(SOURCE_FILES 
    src/main.cpp
    src/GraphController.h
)

# 4. Create Executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# 5. Link Dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE drogon CURL::libcurl)

# 6. Include Paths
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ExecuteEngine
)
