if(WIN32)
    set(PLATFORM_LIBS "")
    set(PLATFORM_SOURCES src/win32_window.cpp)
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
    set(PLATFORM_LIBS PkgConfig::XCB)
    set(PLATFORM_SOURCES src/xcb_window.cpp)
endif()

add_library(window STATIC
    ${PLATFORM_SOURCES}
)

target_include_directories(window
    PUBLIC include/
)

target_compile_features(window PUBLIC cxx_std_20)

target_link_libraries(window PRIVATE
    ${PLATFORM_LIBS}
)

add_subdirectory(tests)
