cmake_minimum_required(VERSION 3.16)

project(IdyllwildApp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Multimedia)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Multimedia)

set(PROJECT_SOURCES
        main.cpp
        mainidyll.cpp
        mainidyll.h
        mainidyll.ui
        session.cpp
        createinquiry.cpp
        repairexport.cpp
        IdyllwildSync.h
        pendinginquiries.cpp
        inquiryrecords.cpp
        inquiryarchive.cpp
        firestore.cpp
        firestore.h
        preferredcontact.cpp
        preferredcontact.h
        sounds.qrc
        branding.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(IdyllwildApp
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET IdyllwildApp APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(IdyllwildApp SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(IdyllwildApp
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(IdyllwildApp PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Multimedia)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.IdyllwildApp)
endif()
set_target_properties(IdyllwildApp PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

set(IDYLLWILD_SPARKLE_DIR "" CACHE PATH "Directory containing Sparkle.framework")
set(IDYLLWILD_UPDATE_PUBLIC_KEY "" CACHE STRING "Sparkle EdDSA public key")
set(IDYLLWILD_UPDATE_REPOSITORY "" CACHE STRING "GitHub repository hosting updates")
set(IDYLLWILD_UPDATE_BUILD "" CACHE STRING "Monotonically increasing update build number")
set(IDYLLWILD_UPDATE_TAG "" CACHE STRING "GitHub release tag for this build")

if(IDYLLWILD_UPDATE_PUBLIC_KEY)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR NOT QT_VERSION_MAJOR EQUAL 6
       OR NOT EXISTS "${IDYLLWILD_SPARKLE_DIR}/Sparkle.framework/Sparkle"
       OR NOT IDYLLWILD_UPDATE_PUBLIC_KEY MATCHES "^[A-Za-z0-9+/]+=$"
       OR NOT IDYLLWILD_UPDATE_REPOSITORY MATCHES "^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"
       OR NOT IDYLLWILD_UPDATE_BUILD MATCHES "^[1-9][0-9]*$"
       OR NOT IDYLLWILD_UPDATE_TAG MATCHES "^macos-build-[0-9]+$")
        message(FATAL_ERROR "macOS updates require Qt 6, Sparkle, an EdDSA public key, and release metadata.")
    endif()

    enable_language(OBJCXX)
    set(IDYLLWILD_UPDATE_FEED
        "https://github.com/${IDYLLWILD_UPDATE_REPOSITORY}/releases/download/${IDYLLWILD_UPDATE_TAG}/appcast.xml")
    target_sources(IdyllwildApp PRIVATE macos/updates.mm)
    target_compile_options(IdyllwildApp PRIVATE
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>"
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-F${IDYLLWILD_SPARKLE_DIR}>")
    target_compile_definitions(IdyllwildApp PRIVATE IDYLLWILD_MAC_UPDATES
        IDYLLWILD_UPDATE_REPOSITORY="${IDYLLWILD_UPDATE_REPOSITORY}"
        IDYLLWILD_UPDATE_BUILD=${IDYLLWILD_UPDATE_BUILD})
    target_link_libraries(IdyllwildApp PRIVATE "${IDYLLWILD_SPARKLE_DIR}/Sparkle.framework" "-framework Foundation")
    set_target_properties(IdyllwildApp PROPERTIES
        OBJCXX_STANDARD 17
        OBJCXX_STANDARD_REQUIRED TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER com.idyllwild.IdyllwildApp
        MACOSX_BUNDLE_BUNDLE_VERSION "${IDYLLWILD_UPDATE_BUILD}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.0"
        BUILD_RPATH "@executable_path/../Frameworks")
    add_custom_command(TARGET IdyllwildApp POST_BUILD
        COMMAND ditto "${IDYLLWILD_SPARKLE_DIR}/Sparkle.framework"
            "$<TARGET_BUNDLE_DIR:IdyllwildApp>/Contents/Frameworks/Sparkle.framework"
        VERBATIM)
endif()

include(GNUInstallDirs)
install(TARGETS IdyllwildApp
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(IdyllwildApp)
endif()
