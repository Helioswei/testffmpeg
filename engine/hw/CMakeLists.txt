#设置-std = c++11
SET(CMAKE_CXX_STANDARD 11)
#把当前目录下的所有源代码和头文件放入到变量HW_DIR
AUX_SOURCE_DIRECTORY(. HW_DIR)

#将当前目录下的文件编译成共享库
ADD_EXECUTABLE(hw ${HW_DIR})

#设置可执行文件的输出目录
SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/leopardlib)

#指明共享库需要链接的其他链接库
TARGET_LINK_LIBRARIES(hw  avformat avcodec avutil swscale avfilter )
