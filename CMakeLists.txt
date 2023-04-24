cmake_minimum_required(VERSION 2.8.9)
set(COMPONENT_NAME mqttcmdsrv)  #set the name of the binary
project(${COMPONENT_NAME})

file(GLOB SOURCES "src/*.c*")
include_directories("./inc/")
FIND_PACKAGE(CMOCKA REQUIRED)
add_executable(${COMPONENT_NAME} ${SOURCES})

# Specify component's dependency.
target_link_libraries(${COMPONENT_NAME} pthread cmocka cjson)
install(TARGETS ${COMPONENT_NAME} DESTINATION usr/bin/)
file(GLOB  INSTALLFILES "*.conf")
install(FILES ${INSTALLFILES} DESTINATION etc/supervisor.d/)


#for cross compiling
#cmake -H. -BOutput -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain-arm-backup.cmake -DCMAKE_INSTALL_PREFIX=/home/user/tmp/
#cmake --build Output -- all
#cmake --build Output -- install

#for local compiling
#cmake -H. -BOutput
#cmake --build Output -- all

#for cleaning make generated output
#rm -rf Output
