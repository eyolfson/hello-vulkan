# Copyright 2016 Jonathan Eyolfson
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License version 3 as published by the Free
# Software Foundation.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# this program. If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.1.3)

project(HELLO_VULKAN C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wextra)

find_package(PkgConfig)

pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_get_variable(WAYLAND_PROTOCOLS_DATADIR wayland-protocols pkgdatadir)
pkg_check_modules(WAYLAND_SCANNER REQUIRED wayland-scanner)

add_custom_command(
	OUTPUT ${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.h
	COMMAND wayland-scanner
	ARGS client-header
	     ${WAYLAND_PROTOCOLS_DATADIR}/unstable/xdg-shell/xdg-shell-unstable-v6.xml
	     ${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.h
)

add_custom_command(
	OUTPUT ${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.c
	COMMAND wayland-scanner
	ARGS code
	     ${WAYLAND_PROTOCOLS_DATADIR}/unstable/xdg-shell/xdg-shell-unstable-v6.xml
	     ${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.c
)

include_directories(
	${CMAKE_BINARY_DIR}
	${WAYLAND_CLIENT_INCLUDE_DIRS}
)

add_executable(hello-vulkan
	main.c
	${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.h
	${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.c
)
target_link_libraries(hello-vulkan
	vulkan
	wayland-client
)
