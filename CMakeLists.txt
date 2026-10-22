cmake_minimum_required(VERSION 3.16)

project(VLEStudio VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(pybind11 CONFIG REQUIRED)

add_executable(VLEStudio
    src/main.cpp
)

target_link_libraries(VLEStudio PRIVATE
    Qt6::Widgets
    Qt6::Sql
    pybind11::embed
)

target_compile_definitions(VLEStudio PRIVATE
    QT_NO_KEYWORDS
    VLE_PYTHON_SITE_PACKAGES="${CMAKE_SOURCE_DIR}/.venv/lib/python3.12/site-packages"
    VLE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
