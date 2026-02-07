cmake_minimum_required(VERSION 3.20)
project(PurpleReaction LANGUAGES CXX)

add_executable(PurpleReaction WIN32
    src/main.cpp
)

target_compile_features(PurpleReaction PRIVATE cxx_std_17)
target_compile_definitions(PurpleReaction PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(PurpleReaction PRIVATE
    d3d11
    dxgi
    user32
    gdi32
    shell32
)
