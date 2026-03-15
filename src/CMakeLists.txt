add_executable(${PROJECT_NAME}
    main.cpp
    volk.cpp
    instance.cpp
    device.cpp
    surface.cpp
    swapchain.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    window
    signal
    ${CMAKE_DL_LIBS}
)
