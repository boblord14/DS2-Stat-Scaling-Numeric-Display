cmake_minimum_required(VERSION 3.27)
project(ds2_stat_scaling_fixer)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG master)
FetchContent_MakeAvailable(minhook)

add_library(statFixer SHARED dllmain.cpp
        Signature.h
        signature.cpp
)

target_link_libraries(statFixer PRIVATE minhook)