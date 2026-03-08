find_package(Vulkan REQUIRED)

if(WIN32)
    set(PLATFORM_DEFINES VK_USE_PLATFORM_WIN32_KHR)
    set(PLATFORM_LIBS "")

elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
    set(PLATFORM_DEFINES VK_USE_PLATFORM_XCB_KHR)
    set(PLATFORM_LIBS PkgConfig::XCB)
endif()

add_executable(${PROJECT_NAME}
    main.cpp
    volk.cpp
)

if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE window/win32_window.cpp)
elseif(UNIX)
    target_sources(${PROJECT_NAME} PRIVATE window/xcb_window.cpp)
endif()

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/window"
    "${Vulkan_INCLUDE_DIRS}"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    VK_NO_PROTOTYPES
    ${PLATFORM_DEFINES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${PLATFORM_LIBS}
    ${CMAKE_DL_LIBS}
)
