cmake_minimum_required(VERSION 3.10)
project(md_viewer)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Add executable
add_executable(md_viewer main.cpp)

# Windows specific settings
if(WIN32)
    set_property(TARGET md_viewer PROPERTY WIN32_EXECUTABLE TRUE)
    
    # Link Windows libraries needed by webview
    target_link_libraries(md_viewer 
        ole32 
        comctl32 
        oleaut32 
        uuid
    )
    
    # For MSVC, set subsystem to windows
    if(MSVC)
        set_target_properties(md_viewer PROPERTIES 
            LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
        )
    endif()
endif()

# Copy executable to root directory after build
add_custom_command(TARGET md_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:md_viewer> ${CMAKE_CURRENT_SOURCE_DIR}
)