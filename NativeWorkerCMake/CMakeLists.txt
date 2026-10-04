cmake_minimum_required(VERSION 3.25)
project(Mu2026NativeWorkers LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Mu2026 native workers require 32-bit Windows")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(Mu2026VlWorker
    "${ROOT}/NativeWorker/VlNativeWorker.cpp"
    "${ROOT}/Source/LeImageLoader.cpp"
    "${ROOT}/Source/NativeVlEngine.cpp"
)
target_include_directories(Mu2026VlWorker PRIVATE "${ROOT}/Source")
target_compile_definitions(Mu2026VlWorker PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_options(Mu2026VlWorker PRIVATE -municode -static -static-libgcc -static-libstdc++)
set_target_properties(Mu2026VlWorker PROPERTIES OUTPUT_NAME "mu2026-vl-worker")

add_executable(Mu2026SgWorker
    "${ROOT}/NativeWorker/SgNativeWorker.cpp"
    "${ROOT}/Source/LeImageLoader.cpp"
    "${ROOT}/Source/NativeSgEngine.cpp"
)
target_include_directories(Mu2026SgWorker PRIVATE "${ROOT}/Source")
target_compile_definitions(Mu2026SgWorker PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_options(Mu2026SgWorker PRIVATE -municode -static -static-libgcc -static-libstdc++)
set_target_properties(Mu2026SgWorker PROPERTIES OUTPUT_NAME "mu2026-sg-worker")
