cmake_minimum_required(VERSION 3.20)
project(BonDriver_GVMVPHZ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(BonDriver_GVMVPHZ SHARED
    src/dllmain.cpp
    src/bon_driver.cpp
    src/winusb_dev.cpp
    src/saa7164.cpp
    src/saigawa.cpp
    src/ivdrm.cpp
    src/BonDriver_GVMVPHZ.def
)

target_compile_definitions(BonDriver_GVMVPHZ PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(BonDriver_GVMVPHZ PRIVATE
    winusb
    setupapi
    bcrypt
)

if(MSVC)
    target_compile_options(BonDriver_GVMVPHZ PRIVATE /W4 /utf-8)
else()
    target_compile_options(BonDriver_GVMVPHZ PRIVATE -Wall -Wextra)
endif()

set_target_properties(BonDriver_GVMVPHZ PROPERTIES
    PREFIX ""
    OUTPUT_NAME "BonDriver_GVMVPHZ"
)

add_executable(bon_test tests/bon_test.cpp)
target_compile_definitions(bon_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(bon_test PRIVATE /W4 /utf-8)
else()
    target_compile_options(bon_test PRIVATE -Wall -Wextra)
endif()
