cmake_minimum_required(VERSION 3.22)

project(WeatherClockHost VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Network Widgets Test)
qt_standard_project_setup()

qt_add_library(host_core STATIC
    src/FrameParser.h
    src/FrameParser.cpp
    src/WeatherText.h
    src/WeatherText.cpp
    src/DeviceModel.h
    src/DeviceModel.cpp
    src/TcpWorker.h
    src/TcpWorker.cpp
)
target_include_directories(host_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(host_core PUBLIC Qt6::Network)
target_compile_features(host_core PUBLIC cxx_std_17)

qt_add_library(host_ui STATIC
    src/MainWindow.h
    src/MainWindow.cpp
)
target_include_directories(host_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(host_ui PUBLIC host_core Qt6::Widgets)

qt_add_executable(WeatherClockHost
    src/main.cpp
)
target_link_libraries(WeatherClockHost PRIVATE host_ui)
set_target_properties(WeatherClockHost PROPERTIES WIN32_EXECUTABLE TRUE)

include(CTest)

qt_add_executable(host_tests
    tests/host_tests.cpp
)
target_link_libraries(host_tests PRIVATE host_core Qt6::Test)
set_target_properties(host_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME host_tests COMMAND host_tests)

qt_add_executable(host_smoke
    tests/smoke.cpp
)
target_link_libraries(host_smoke PRIVATE host_ui)
set_target_properties(host_smoke PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME host_smoke COMMAND host_smoke)

# 测试直接跑构建目录里的 exe，需要找到 Qt 的 DLL。
get_target_property(_qt_core_dll Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT _qt_core_dll)
    get_target_property(_qt_core_dll Qt6::Core IMPORTED_LOCATION)
endif()
get_filename_component(_qt_bin_dir "${_qt_core_dll}" DIRECTORY)
set_tests_properties(host_tests host_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}"
)
set_tests_properties(host_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
