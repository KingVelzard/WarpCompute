cmake_minimum_required(VERSION 3.24)
project(GPUSchedulingServer, LANGUAGES CXX CUDA)

# for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CUDAToolkit REQUIRED)

# define C++ / CUDA standards
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

set(CMAKE_CUDA_ARCHITECTURES 120)

file(GLOB GPU_SOURCES
  "src/gpu/*.cu"
  "src/gpu/*.h"
  "src/gpu/*.cuh"
)


add_library(cuda_kernels STATIC ${GPU_SOURCES})

set_target_properties(cuda_kernels PROPERTIES
  CUDA_SEPARABLE_COMPILATION ON
  POSITION_INDEPENDENT_CODE ON
)

# expose the internal gpu folder

target_include_directories(cuda_kernels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu)

target_compile_options(cuda_kernels PRIVATE
  $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-Wall;-Wextra>
)

# host compiler

file(GLOB HOST_SOURCES
  "src/*.cpp"
)

add_executable(host_app ${HOST_SOURCES})

# include top level headers

target_include_directories(host_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(host_app PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>
)

# link host and device

target_link_libraries(host_app PRIVATE cuda_kernels CUDA::cudart)
set_target_properties(host_app PROPERTIES CUDA_RESOLVE_DEVICE_SYMBOLS ON)

