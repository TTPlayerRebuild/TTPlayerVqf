find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(TTP_FFMPEG_BASH "" CACHE FILEPATH "Optional Git for Windows bash.exe")
set(TTP_FFMPEG_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg" CACHE PATH "Private FFmpeg source/build cache")
set(_ffmpeg_bash)
if(TTP_FFMPEG_BASH)
  list(APPEND _ffmpeg_bash --bash "${TTP_FFMPEG_BASH}")
endif()
set(TTP_FFMPEG_SOURCE "" CACHE PATH "Optional complete FFmpeg source (source release bundle)")
if(NOT TTP_FFMPEG_SOURCE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/configure")
  set(TTP_FFMPEG_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")
endif()
if(TTP_FFMPEG_SOURCE)
  list(APPEND _ffmpeg_bash --source "${TTP_FFMPEG_SOURCE}")
  set(_ffmpeg_source "${TTP_FFMPEG_SOURCE}")
else()
  set(_ffmpeg_source "${TTP_FFMPEG_BUILD}/ffmpeg-9.0.2")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/make-4.4.1.tar.gz")
  list(APPEND _ffmpeg_bash --make-archive "${CMAKE_CURRENT_SOURCE_DIR}/third_party/make-4.4.1.tar.gz")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-source-path.txt" "${_ffmpeg_source}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-build-path.txt" "${TTP_FFMPEG_BUILD}")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_ffmpeg.py"
  --build "${TTP_FFMPEG_BUILD}" --vs "${CMAKE_GENERATOR_INSTANCE}"
  --ltl "${ttplayer_vc_ltl_SOURCE_DIR}" --kind vqf ${_ffmpeg_bash}
  COMMAND_ERROR_IS_FATAL ANY)
add_library(codec INTERFACE)
target_include_directories(codec INTERFACE "${_ffmpeg_source}"
  "${TTP_FFMPEG_BUILD}/ffmpeg-vqf")
target_link_libraries(codec INTERFACE
  "${TTP_FFMPEG_BUILD}/ffmpeg-vqf/libavformat/avformat.lib"
  "${TTP_FFMPEG_BUILD}/ffmpeg-vqf/libavcodec/avcodec.lib"
  "${TTP_FFMPEG_BUILD}/ffmpeg-vqf/libavutil/avutil.lib" bcrypt)
