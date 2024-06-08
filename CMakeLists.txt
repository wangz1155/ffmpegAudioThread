cmake_minimum_required(VERSION 3.16)

project(ffmpegAudioThread VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)


find_package(Qt6 6.4 REQUIRED COMPONENTS Quick Multimedia)
find_package(FFmpeg REQUIRED)
include_directories(${FFMPEG_INCLUDE_DIRS})

set(FFMPEG_LIBRARIES /usr/lib/x86_64-linux-gnu)

qt_standard_project_setup()

qt_add_executable(appffmpegAudioThread
    main.cpp
)

qt_add_qml_module(appffmpegAudioThread
    URI ffmpegAudioThread
    VERSION 1.0
    QML_FILES
        Main.qml
        SOURCES videoplayer.h videoplayer.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appffmpegAudioThread PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appffmpeg01
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appffmpegAudioThread
    PRIVATE Qt6::Quick Qt6::Multimedia
    ${FFMPEG_LIBRARIES}/libavformat.so
    ${FFMPEG_LIBRARIES}/libavcodec.so
    ${FFMPEG_LIBRARIES}/libavutil.so
    ${FFMPEG_LIBRARIES}/libswscale.so
    ${FFMPEG_LIBRARIES}/libswresample.so
    ${FFMPEG_LIBRARIES}/libavfilter.so
)

include(GNUInstallDirs)
install(TARGETS appffmpegAudioThread
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
