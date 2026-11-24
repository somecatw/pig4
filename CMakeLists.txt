cmake_minimum_required(VERSION 3.16)

# MSVC + Ninja 的 /showIncludes 前缀是本地化的，编码取决于当前控制台代码页：
#   chcp 936   -> GBK 字节
#   chcp 65001 -> UTF-8 字节
# 从终端和 Qt Creator 构建时代码页可能不同，Ninja 只记一种前缀，于是其中一边
# 会静默丢掉头文件依赖（改 .h 不触发重编）。用 launcher 把编译器输出规范成
# 固定 ASCII 前缀，避免依赖具体代码页。
if(WIN32 AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(PIG4_PYTHON NAMES python python3)
    if(PIG4_PYTHON AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/tools/msvc_deps_wrapper.py")
        set(CMAKE_CXX_COMPILER_LAUNCHER
            "${PIG4_PYTHON};${CMAKE_CURRENT_LIST_DIR}/tools/msvc_deps_wrapper.py")
        message(STATUS "MSVC deps wrapper: ${PIG4_PYTHON}")
    endif()
endif()

project(pig4 VERSION 0.1 LANGUAGES CXX)

# 只有在 wrapper 生效时才强制 ASCII 前缀；否则保留 CMake 探测值。
if(WIN32 AND MSVC AND
   "${CMAKE_CXX_COMPILER_LAUNCHER}" MATCHES "msvc_deps_wrapper\\.py")
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "Note: including file:  ")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file:  ")
elseif(WIN32 AND MSVC)
    message(WARNING
        "Python not found or custom compiler launcher in use; "
        "MSVC /showIncludes dependency tracking may be unstable across "
        "console code pages (Qt Creator vs terminal).")
endif()

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# 单一 Qt target；engine/ 和 game/ 只是源码分层，不拆成两个编译单元。
set(PIG4_SOURCES
    engine/mathbase.h
    engine/transform.h engine/transform.cpp
    engine/structures.h
    engine/render.h engine/render.cpp
    engine/parallel_render.h engine/parallel_render.cpp
    engine/shaders.h
    engine/shader_interface.h
    engine/assetmanager.h engine/assetmanager.cpp
    engine/gameobject.h engine/gameobject.cpp
    engine/raytest.h engine/raytest.cpp
    engine/utils.h
    engine/stage3d.h engine/stage3d.cpp

    game/mainwindow.h game/mainwindow.cpp game/mainwindow.ui
    game/gameui.h game/gameui.cpp
    game/dialoguepanel.h game/dialoguepanel.cpp
    game/gameevents.h game/gameevents.cpp
    game/clockart.h game/clockart.cpp
    game/textart.h game/textart.cpp
    game/overlayui.h game/overlayui.cpp
    game/storycatalog.h game/storycatalog.cpp
    game/storyui.h game/storyui.cpp
    game/door.h game/door.cpp
    game/doorsystem.h game/doorsystem.cpp
    game/roommanifest.h game/roommanifest.cpp
    game/buildingmanifest.h game/buildingmanifest.cpp
    game/room.h game/room.cpp
    game/roommanager.h game/roommanager.cpp
    game/item/itemcatalog.h game/item/itemcatalog.cpp
    game/item/itemicons.h game/item/itemicons.cpp
    game/item/itemdrag.h game/item/itemdrag.cpp
    game/item/itemui.h game/item/itemui.cpp
    game/puzzle/puzzle.h game/puzzle/puzzle.cpp
    game/puzzle/puzzleui.h game/puzzle/puzzleui.cpp
    game/puzzle/puzzleart.h game/puzzle/puzzleart.cpp
    game/puzzle/shardart.h game/puzzle/shardart.cpp
    game/puzzle/views/password/passwordart.h game/puzzle/views/password/passwordart.cpp
    game/puzzle/views/password/passwordkeypadview.h game/puzzle/views/password/passwordkeypadview.cpp
    game/puzzle/views/elevator/elevatorart.h game/puzzle/views/elevator/elevatorart.cpp
    game/puzzle/views/elevator/elevatorpanelview.h game/puzzle/views/elevator/elevatorpanelview.cpp
    game/dev/devhooks.h game/dev/devhooks.cpp
    game/main.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(pig4
        MANUAL_FINALIZATION
        ${PIG4_SOURCES}
    )
else()
    add_executable(pig4 ${PIG4_SOURCES})
endif()

target_include_directories(pig4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/game
    ${CMAKE_CURRENT_SOURCE_DIR}/game/item
    ${CMAKE_CURRENT_SOURCE_DIR}/game/puzzle
    ${CMAKE_CURRENT_SOURCE_DIR}/game/puzzle/views
)
target_link_libraries(pig4 PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

set(FAST_FP_FILES "engine/parallel_render.cpp")
if(MSVC)
    set_source_files_properties(${FAST_FP_FILES} PROPERTIES COMPILE_FLAGS "/fp:fast")
else()
    set_source_files_properties(${FAST_FP_FILES} PROPERTIES COMPILE_FLAGS "-ffast-math")
endif()

if(WIN32)
    if(MINGW)
        target_link_options(pig4 PRIVATE -mconsole)
        set_target_properties(pig4 PROPERTIES WIN32_EXECUTABLE FALSE)
    else()
        set_target_properties(pig4 PROPERTIES
            WIN32_EXECUTABLE FALSE
            LINK_FLAGS "/SUBSYSTEM:CONSOLE"
        )
    endif()
endif()

if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.pig4)
endif()
set_target_properties(pig4 PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
)

include(GNUInstallDirs)
install(TARGETS pig4
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(pig4)
endif()
