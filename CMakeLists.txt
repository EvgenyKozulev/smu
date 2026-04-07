cmake_minimum_required(VERSION 3.22)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(SMU_TEST VERSION 1.0.7 LANGUAGES CXX C ASM)
    set(SMU_IS_TOP_LEVEL ON)
else()
    set(SMU_IS_TOP_LEVEL OFF)
endif()

set(MODULE_NAME "SMU")
set(${MODULE_NAME}_VERSION_MAJOR 0)
set(${MODULE_NAME}_VERSION_MINOR 1)
set(${MODULE_NAME}_VERSION_PATCH 0)

add_library(${MODULE_NAME}_INTERFACE INTERFACE)
target_compile_features(${MODULE_NAME}_INTERFACE INTERFACE cxx_std_20)

target_include_directories(
    ${MODULE_NAME}_INTERFACE
    INTERFACE
    inc
)

add_library(${MODULE_NAME})

define_property(TARGET PROPERTY ${MODULE_NAME}_VERSION_FULL
    BRIEF_DOCS "Full version of ${MODULE_NAME}"
    FULL_DOCS "Concatenated Major.Minor.Patch version"
)

set_target_properties(${MODULE_NAME} PROPERTIES
    ${MODULE_NAME}_VERSION_FULL "${${MODULE_NAME}_VERSION_MAJOR}.${${MODULE_NAME}_VERSION_MINOR}.${${MODULE_NAME}_VERSION_PATCH}"
)

target_compile_definitions(${MODULE_NAME}_INTERFACE INTERFACE
    ${MODULE_NAME}_VERSION_MAJOR=${${MODULE_NAME}_VERSION_MAJOR}
    ${MODULE_NAME}_VERSION_MINOR=${${MODULE_NAME}_VERSION_MINOR}
    ${MODULE_NAME}_VERSION_PATCH=${${MODULE_NAME}_VERSION_PATCH}
)

target_sources(
    ${MODULE_NAME}
    PRIVATE
    src/smu.cpp
    src/smu_tree.cpp
    src/smu_fmb.cpp
    src/smu_tab.cpp
)

target_link_libraries(
    ${MODULE_NAME}
    ${MODULE_NAME}_INTERFACE
)
if(SMU_IS_TOP_LEVEL)
    include(CTest)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY git@github.com:google/googletest.git # <-- Смените на это
        GIT_TAG v1.14.0 # или нужная вам версия
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(BUILD_GTEST ON CACHE BOOL "" FORCE)


    FetchContent_MakeAvailable(googletest)

    FetchContent_Declare(
        rapidjson
        GIT_REPOSITORY git@github.com:Tencent/rapidjson.git
        GIT_TAG master
    )

    set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(rapidjson)

    include(GoogleTest)

    add_executable(
        ${PROJECT_NAME}
        test/src/test0.cpp
        test/src/test1.cpp
        test/src/test2.cpp
        test/src/test3.cpp
        test/src/test4.cpp
    )

    target_include_directories(
        ${PROJECT_NAME}
        PRIVATE
        ${rapidjson_SOURCE_DIR}/include
    )

    target_link_libraries(
        ${PROJECT_NAME}
        ${MODULE_NAME}
        gtest_main
    )

    gtest_discover_tests(${PROJECT_NAME})
endif()
