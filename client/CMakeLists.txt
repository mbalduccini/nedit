cmake_minimum_required(VERSION 3.0)

find_package(Qt5 5.5.0 REQUIRED Core Network)

if(WIN32)
	set(APP_ICON_RESOURCE_WINDOWS "../src/res/nedit-ng.rc")
endif()

add_executable(ncl
	nc.cpp
	${APP_ICON_RESOURCE_WINDOWS}
)

target_link_libraries(ncl PUBLIC
	Qt5::Core
	Qt5::Network
	Util
	Settings
	Boost::boost
)

target_add_warnings(ncl)

set_property(TARGET ncl PROPERTY CXX_STANDARD 14)
set_property(TARGET ncl PROPERTY CXX_EXTENSIONS OFF)
set_property(TARGET ncl PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(TARGET_COMPILER_MSVC)
	set_property(TARGET ncl PROPERTY WIN32_EXECUTABLE ON)
endif()

install(TARGETS ncl DESTINATION bin)
