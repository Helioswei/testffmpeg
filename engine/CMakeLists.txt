#cmake 最低版本需求，不加入此行会受到警告信息
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

#项目的名称
PROJECT(TESTFFMPEG)

#设置-std = c++11
SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_FLAGS "-std=c++11")
#ADD_DEFINITIONS("-Wall -g")
SET(CMAKE_BUILD_TYPE "Debug")
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")

#获取系统环境变量
#SET(PROJECTPATH $ENV{LEOPARDV4})

#设置protobuf自动构建的路径
#SET(PROTOBUF_INCLUDE_DIR ${PROJECTPATH}/deps/protobuf/include)
#SET(PROTOBUF_LIBRARY ${PROJECTPATH}/deps/protobuf/lib)
#SET(PROTOBUF_PROTOC_EXECUTABLE ${PROJECTPATH}/deps/protobuf/bin/protoc)

#设置外部库头文件的路径
#INCLUDE_DIRECTORIES(${PROJECTPATH}/deps/common)
#INCLUDE_DIRECTORIES(${PROJECTPATH}/deps/video/include)
INCLUDE_DIRECTORIES(/usr/include/ffmpeg/)

#设置外部库动态库的路径
#LINK_DIRECTORIES(${PROJECTPATH}/deps/common/leopardlib)
#LINK_DIRECTORIES(${PROJECTPATH}/deps/video/lib)
#设置项目的子模块
#ADD_SUBDIRECTORY(proto)
#ADD_SUBDIRECTORY(media)
#ADD_SUBDIRECTORY(cmd)
#ADD_SUBDIRECTORY(test)
ADD_SUBDIRECTORY(hw)

#把当前目录(.)下所有源代码和头文件加入变量SRC_DIR
AUX_SOURCE_DIRECTORY(. SRC_DIR)

#生成应用程序videotool(在windows下会自动生成videotool.exe)
#ADD_EXECUTABLE(videotool ${SRC_DIR})

