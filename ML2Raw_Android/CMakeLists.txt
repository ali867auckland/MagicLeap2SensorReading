cmake_minimum_required(VERSION 3.22)
project(mldepth_unity)

add_library(mldepth_unity SHARED
  src/mlperception_service.cpp
  src/mlheadtracking.cpp
  src/mlrgbcamera.cpp
  src/mlcvcamera.cpp
  src/mlworldcam.cpp
  src/mldepth.cpp
)

target_include_directories(mldepth_unity PRIVATE
  /Users/azyl/MagicLeap/mlsdk/v1.12.0/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_directories(mldepth_unity PRIVATE
  /Users/azyl/MagicLeap/mlsdk/v1.12.0/lib/ml2
)

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(mldepth_unity
  perception.magicleap
  ${log-lib}
  ${android-lib}
)

set_target_properties(mldepth_unity PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
)
