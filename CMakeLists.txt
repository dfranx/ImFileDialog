cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
project(ImFileDialogExample)

if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DIFD_USE_OPENGL -g -ggdb -ObjC++")
else() 
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DIFD_USE_OPENGL -g -ggdb")
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# source code
set(SOURCES
    example.cpp
    ImFileDialog.cpp

    libs/imgui/imgui.cpp
    libs/imgui/imgui_widgets.cpp
    libs/imgui/imgui_draw.cpp
    libs/imgui/imgui_tables.cpp
    libs/imgui/backends/imgui_impl_opengl3.cpp
    libs/imgui/backends/imgui_impl_sdl2.cpp
    libs/apifilesystem/filesystem.cpp
	
)

# cmake toolchain
if(CMAKE_TOOLCHAIN_FILE)
    include(${CMAKE_TOOLCHAIN_FILE})
endif(CMAKE_TOOLCHAIN_FILE)

# pkg
if (UNIX)
  find_package(PkgConfig REQUIRED)
endif()

# sdl2
if(WIN32 OR APPLE)
  find_package(SDL2 REQUIRED)
endif()

# opengl
set(OpenGL_GL_PREFERENCE GLVND CACHE STRING "Linux only: if GLVND, use the vendor-neutral GL libraries (default). If LEGACY, use the legacy ones (might be necessary to have working optirun/primusrun)")
set_property(CACHE OpenGL_GL_PREFERENCE PROPERTY STRINGS GLVND LEGACY)
find_package(OpenGL REQUIRED)

# glew
list(APPEND CMAKE_PREFIX_PATH "C:/vcpkg/packages/glew_x64-windows")
find_package(GLEW REQUIRED)

# create executable
add_executable(ImFileDialogExample ${SOURCES})

# properties
set_target_properties(ImFileDialogExample PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
)

# include directories
target_include_directories(ImFileDialogExample PRIVATE ${SDL2_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIRS})
target_include_directories(ImFileDialogExample PRIVATE libs libs/imgui)

# link libraries
target_link_libraries(ImFileDialogExample ${OPENGL_LIBRARIES})

if(WIN32)
	# link specific win32 libraries
	target_link_libraries(ImFileDialogExample GLEW::GLEW SDL2::SDL2 SDL2::SDL2main shell32)

elseif(UNIX AND NOT APPLE)
	# link non-apple unix libraries
	add_subdirectory(libs/lunasvg)
	include_directories(libs/lunasvg/include /usr/include/SDL2 /usr/local/include/SDL2)
	find_package(PkgConfig REQUIRED)
	pkg_search_module(PKG_SDL2 REQUIRED IMPORTED_TARGET sdl2)
	pkg_search_module(PKG_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
	pkg_search_module(PKG_GIO2 REQUIRED IMPORTED_TARGET gio-2.0)
	pkg_search_module(PKG_GLIB2 REQUIRED IMPORTED_TARGET glib-2.0)
	if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
		target_link_libraries(ImFileDialogExample lunasvg PkgConfig::PKG_SDL2 PkgConfig::PKG_GTK3 PkgConfig::PKG_GIO2 PkgConfig::PKG_GLIB2 ${GLEW_LIBRARIES} ${CMAKE_DL_LIBS} kvm pthread)
	else()
		target_link_libraries(ImFileDialogExample lunasvg PkgConfig::PKG_SDL2 PkgConfig::PKG_GTK3 PkgConfig::PKG_GIO2 PkgConfig::PKG_GLIB2 ${GLEW_LIBRARIES} ${CMAKE_DL_LIBS} pthread)
	endif()
elseif(APPLE)
	target_link_libraries(ImFileDialogExample "-framework CoreFoundation -framework AppKit" GLEW::GLEW ${SDL2_LIBRARIES} ${CMAKE_DL_LIBS})
endif()
