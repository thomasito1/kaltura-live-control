cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# Both are required for this plugin: the dock needs obs-frontend-api, and the UI is Qt.
# They stay as options so the template's cmake/ machinery keeps working unmodified.
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# The macOS keychain backend is Objective-C++ (Security.framework).
if(APPLE)
  enable_language(OBJCXX)
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  # Deliberately NOT Qt6::Network. OBS ships Qt6Network.dll but no Qt TLS backend
  # plugin (no tls/qopensslbackend.dll, no qschannelbackend.dll), so every https
  # request fails with "TLS initialization failed". HTTP goes through libcurl below,
  # which is what OBS itself uses.
  #
  # Svg is safe: QSvgRenderer lives in the module itself and needs no image-format
  # plugin, and Qt6Svg.dll ships with OBS.
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# OBS already ships libcurl.dll in bin/64bit, so linking it adds nothing for the user
# to install - and unlike Qt Network it can actually negotiate TLS here.
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/resources.qrc
    src/kaltura/HttpClient.cpp
    src/kaltura/HttpClient.hpp
    src/kaltura/AudioTracks.cpp
    src/kaltura/AudioTracks.hpp
    src/kaltura/Endpoints.cpp
    src/kaltura/Endpoints.hpp
    src/kaltura/KalturaClient.cpp
    src/kaltura/KalturaClient.hpp
    src/obs/MultiOutput.cpp
    src/obs/MultiOutput.hpp
    src/obs/StreamService.cpp
    src/obs/StreamService.hpp
    src/obs/HealthMonitor.cpp
    src/obs/HealthMonitor.hpp
    src/ui/StreamsPanel.cpp
    src/ui/StreamsPanel.hpp
    src/ui/KalturaDock.cpp
    src/ui/KalturaDock.hpp
    src/platform/Credentials.hpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# --- Per-platform credential storage -----------------------------------------
# Spec section 4: the admin secret must live in the OS keychain, never a config file.
if(WIN32)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/platform/Credentials_win.cpp)
  # CredWriteW / CredReadW / CredDeleteW
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE advapi32)
elseif(APPLE)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/platform/Credentials_mac.mm)
  find_library(SECURITY_FRAMEWORK Security REQUIRED)
  find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${SECURITY_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
else()
  # Linux is not a shipping target; keep the build green with an in-memory stub.
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/platform/Credentials_stub.cpp)
endif()

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
