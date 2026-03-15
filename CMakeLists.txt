cmake_minimum_required(VERSION 3.20)
project(peerjam_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(PEERJAM_ENABLE_PORTAUDIO "Enable PortAudio device I/O backend" OFF)

set(AOO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../sonobus/deps/aoo")
set(AOO_OSCPACK_ROOT "${AOO_ROOT}/deps/oscpack")
set(AOO_MD5_ROOT "${AOO_ROOT}/deps/md5")
set(OPUS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sonobus/deps/mac/include")
set(OPUS_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../sonobus/deps/mac/lib/libopus.a")

add_library(oscpack STATIC
  ${AOO_OSCPACK_ROOT}/osc/OscTypes.cpp
  ${AOO_OSCPACK_ROOT}/osc/OscReceivedElements.cpp
  ${AOO_OSCPACK_ROOT}/osc/OscPrintReceivedElements.cpp
  ${AOO_OSCPACK_ROOT}/osc/OscOutboundPacketStream.cpp
)
target_include_directories(oscpack PUBLIC
  ${AOO_OSCPACK_ROOT}
)

add_library(aoo STATIC
  ${AOO_MD5_ROOT}/md5.c
  ${AOO_ROOT}/lib/src/client.cpp
  ${AOO_ROOT}/lib/src/server.cpp
  ${AOO_ROOT}/lib/src/source.cpp
  ${AOO_ROOT}/lib/src/sink.cpp
  ${AOO_ROOT}/lib/src/common.cpp
  ${AOO_ROOT}/lib/src/net_utils.cpp
  ${AOO_ROOT}/lib/src/sync.cpp
  ${AOO_ROOT}/lib/src/time.cpp
  ${AOO_ROOT}/lib/src/codec_pcm.cpp
  ${AOO_ROOT}/lib/src/codec_opus.cpp
)
target_compile_definitions(aoo PUBLIC AOO_STATIC AOO_BUILD)
target_include_directories(aoo PUBLIC
  ${AOO_ROOT}/lib
  ${AOO_ROOT}/deps
  ${AOO_OSCPACK_ROOT}
  ${AOO_MD5_ROOT}
  ${OPUS_INCLUDE_DIR}
)
target_link_libraries(aoo PUBLIC oscpack "${OPUS_LIB}")

if(APPLE)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
  target_link_libraries(aoo PUBLIC ${COREFOUNDATION_FRAMEWORK})
endif()

add_library(peerjam_measure STATIC
  src/app/cli_options.cpp
  src/app/config.cpp
  src/app/session_profile.cpp
  src/common/logger.cpp
  src/logic/audio/audio_stream.cpp
  src/logic/audio/audio_runtime.cpp
  src/logic/network/candidate.cpp
  src/logic/network/candidate_exchange.cpp
  src/logic/network/candidate_gatherer.cpp
  src/logic/network/path_manager.cpp
  src/logic/network/path_probe_engine.cpp
  src/logic/network/path_scorer.cpp
  src/logic/probe/probe_packet.cpp
  src/logic/probe/probe_runtime.cpp
  src/logic/probe/measurement.cpp
  src/logic/transport/udp_datagram_socket.cpp
  src/logic/control/aoo_peer_client.cpp
)
target_include_directories(peerjam_measure PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${AOO_ROOT}/lib
)
target_link_libraries(peerjam_measure PUBLIC aoo)

if(PEERJAM_ENABLE_PORTAUDIO)
  find_path(PORTAUDIO_INCLUDE_DIR portaudio.h)
  find_library(PORTAUDIO_LIBRARY portaudio)
  if(PORTAUDIO_INCLUDE_DIR AND PORTAUDIO_LIBRARY)
    target_include_directories(peerjam_measure PUBLIC ${PORTAUDIO_INCLUDE_DIR})
    target_link_libraries(peerjam_measure PUBLIC ${PORTAUDIO_LIBRARY})
    target_compile_definitions(peerjam_measure PUBLIC PEERJAM_HAS_PORTAUDIO=1)
  else()
    message(WARNING "PEERJAM_ENABLE_PORTAUDIO=ON but PortAudio not found; building without device I/O backend")
  endif()
endif()

add_executable(pj-signal-server src/signal_server_main.cpp)
target_include_directories(pj-signal-server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${AOO_ROOT}/lib
)
target_link_libraries(pj-signal-server PRIVATE aoo peerjam_measure)

add_executable(pj-peer-probe src/peer_probe_main.cpp)
target_link_libraries(pj-peer-probe PRIVATE peerjam_measure)

add_executable(pj-net-test src/net_test_main.cpp)
target_link_libraries(pj-net-test PRIVATE peerjam_measure)
