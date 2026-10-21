cmake_minimum_required(VERSION 3.22)
project(osc_toolkit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
set(PRELOADER_TAG "main" CACHE STRING "preloader-android tag or branch")
FetchContent_Declare(preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG ${PRELOADER_TAG})
FetchContent_MakeAvailable(preloader_android)

add_library(osc_toolkit SHARED src/main.cpp)
target_link_libraries(osc_toolkit PRIVATE preloader)
