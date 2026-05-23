cmake_minimum_required(VERSION 3.20)
project(JRSChatCPP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(JRSChatCPP WIN32
    src/main.cpp
    src/app_settings.cpp
    src/network_helper.cpp
    src/packet_capture_service.cpp
    src/packet_crypt.cpp
    src/item_dat_loader.cpp
    src/chat_packet_processor.cpp
    src/wanted_regex_service.cpp
    src/item_packet_processor.cpp
    src/sound_notification_service.cpp
    src/bell_processor.cpp
    src/chat_logger_service.cpp
    src/item_logger_service.cpp
    src/process_monitor_service.cpp
    src/settings_dialog.cpp
    src/rate_roten_processor.cpp
    src/roten_processor.cpp
    src/gold_processor.cpp
    src/dc_manager.cpp
    src/dc_setting_dialog.cpp
    src/drop_packet_processor.cpp
    src/special_drop_processor.cpp
    src/equipment_packet_processor.cpp
    src/screen_size_dialog.cpp
    src/wanted_regex_dialog.cpp
    src/guild_member_processor.cpp
    src/guild_compare_window.cpp
    src/resource.rc
)

target_compile_definitions(JRSChatCPP PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)

if(MSVC)
    target_compile_options(JRSChatCPP PRIVATE /W4 /permissive- /utf-8)
endif()

target_link_libraries(JRSChatCPP PRIVATE comctl32 advapi32 user32 gdi32 shell32 ole32 oleaut32 winmm gdiplus iphlpapi ws2_32)

# Boost.Regex (後読み・ Unicodeプロパティ対応)
find_package(boost_regex CONFIG REQUIRED)
target_link_libraries(JRSChatCPP PRIVATE Boost::regex)

# Removed uchardet integration (reverted to original SJIS-only decoding)

# 管理者権限マニフェスト埋め込み + MAPファイル + PDB生成
if(MSVC)
    target_link_options(JRSChatCPP PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'" "/MAP" "/DEBUG")
    target_compile_options(JRSChatCPP PRIVATE /Zi)
endif()

### No runtime copy commands required for uchardet (integration removed)

target_include_directories(JRSChatCPP PRIVATE ${CMAKE_SOURCE_DIR}/src)
