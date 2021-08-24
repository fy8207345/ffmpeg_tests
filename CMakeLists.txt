cmake_minimum_required(VERSION 3.0)
project(ffmpeg_tests C)

set(CMAKE_C_STANDARD 99)

include_directories("E:/ffmpeg/source/ShiftMediaProject/msvc/include/")

link_directories("E:/ffmpeg/source/ShiftMediaProject/msvc/lib/x64/")

link_libraries(libavcodecd.lib libavdeviced.lib libavfilterd.lib libavformatd.lib libavutild.lib libswresampled.lib libswscaled.lib libpostprocd.lib)

link_libraries(msvcrtd.lib)

add_executable(ffmpeg_tests qsvdec.c transcoding.c)
