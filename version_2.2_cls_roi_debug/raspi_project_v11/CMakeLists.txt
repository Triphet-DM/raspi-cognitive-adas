cmake_minimum_required(VERSION 3.16)
project(pi5_ai_phase1_ncnn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Development)
find_package(ncnn REQUIRED)
find_package(Threads REQUIRED)

add_executable(app
    src/main.cpp
    src/camera/Picamera2Camera.cpp
    src/inference/NcnnModel.cpp
    src/inference/YoloDetector.cpp
    src/inference/SpeedSignClassifier.cpp
    src/decision/SpeedSignLifecycle.cpp
    src/decision/SignEpisodeLifecycle.cpp
    src/decision/CurrentSpeedLimitManager.cpp
    src/decision/AnnouncementPolicy.cpp
    src/decision/ShadowSpeedLimitPipeline.cpp
    src/decision/MomentaryPolicy.cpp
    src/decision/MomentaryEngine.cpp
    src/decision/BehaviorPolicyRouter.cpp
    src/decision/NotificationArbiter.cpp
    src/audio/SpeedAudioMap.cpp
    src/audio/MomentaryAudioMap.cpp
    src/audio/NotificationManager.cpp
    src/vision/Letterbox.cpp
    src/vision/Nms.cpp
    src/vision/Draw.cpp
)

target_include_directories(app PRIVATE
    src
    ${OpenCV_INCLUDE_DIRS}
    ${Python3_INCLUDE_DIRS}
)

target_link_libraries(app PRIVATE
    ${OpenCV_LIBS}
    Python3::Python
    ncnn
    Threads::Threads
)

target_compile_options(app PRIVATE -O3)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    target_compile_options(app PRIVATE -mcpu=cortex-a76 -mtune=cortex-a76 -ffast-math)
endif()
