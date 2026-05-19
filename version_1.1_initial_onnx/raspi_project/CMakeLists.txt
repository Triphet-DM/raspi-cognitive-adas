# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(pi5_ai_phase1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Development)

if(NOT DEFINED ONNXRUNTIME_ROOT)
    set(ONNXRUNTIME_ROOT "/opt/onnxruntime")
endif()

set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include")
set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_ROOT}/lib")

add_executable(app
    src/main.cpp

    src/camera/Picamera2Camera.cpp

    src/inference/OnnxModel.cpp
    src/inference/YoloDetector.cpp

    src/vision/Letterbox.cpp
    src/vision/Nms.cpp
    src/vision/Draw.cpp
)

target_include_directories(app PRIVATE
    src
    ${OpenCV_INCLUDE_DIRS}
    ${Python3_INCLUDE_DIRS}
    ${ONNXRUNTIME_INCLUDE_DIR}
)

target_link_directories(app PRIVATE
    ${ONNXRUNTIME_LIB_DIR}
)

target_link_libraries(app PRIVATE
    ${OpenCV_LIBS}
    Python3::Python
    onnxruntime
)

target_compile_options(app PRIVATE -O3)
