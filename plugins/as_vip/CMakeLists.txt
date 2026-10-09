# as_vip — Plugify C++ plugin: port of the Pisex admin_system module "VIP".
#
#   plugify-as_vip  the plugin shared library (entry "bin/as_vip" -> bin/libas_vip.so)
#
# The only module consuming BOTH cores at once: admin_api.hpp (HasPermission / OnAdminConnected)
# and vip_api.hpp (VIP_GiveClientVIP). It compiles the generated binding globals of
# plugins/admin_system (__admin_system_*) and plugins/vip (__vip__*); the s2sdk/hudmenu ones come
# in through pisexcompat.

set(AS_VIP_VERSION "1.0.0")

# --- generated consumer bindings --------------------------------------------------
# admin_system_bindings_obj (plugins/admin_system) and vip_bindings (plugins/vip) exist when
# their directories configure first; GLOB order is not guaranteed, so a missing target is
# replaced by a private OBJECT library over the same checked-in generated .cpp (the
# lr_vip_multi pattern).
if(TARGET admin_system_bindings_obj)
    set(AS_VIP_ADMIN_BINDING_OBJECTS "$<TARGET_OBJECTS:admin_system_bindings_obj>")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/plugins/admin_system/include/external/plugify/include/admin_system/admin_system.cpp")
    add_library(admin_system_bindings_as_vip OBJECT
            "${CMAKE_SOURCE_DIR}/plugins/admin_system/include/external/plugify/include/admin_system/admin_system.cpp")
    target_include_directories(admin_system_bindings_as_vip SYSTEM PUBLIC
            "${CMAKE_SOURCE_DIR}/plugins/admin_system/include/external/plugify/include"
            "${CMAKE_SOURCE_DIR}/sdk/include")
    target_compile_definitions(admin_system_bindings_as_vip PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
    set(AS_VIP_ADMIN_BINDING_OBJECTS "$<TARGET_OBJECTS:admin_system_bindings_as_vip>")
else()
    message(FATAL_ERROR "as_vip needs the generated admin_system consumer bindings (plugins/admin_system/include/external/plugify)")
endif()

if(TARGET vip_bindings)
    set(AS_VIP_VIP_BINDING_OBJECTS "$<TARGET_OBJECTS:vip_bindings>")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include/vip/vip.cpp")
    add_library(vip_bindings_as_vip OBJECT
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include/vip/vip.cpp")
    target_include_directories(vip_bindings_as_vip SYSTEM PUBLIC
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
            "${CMAKE_SOURCE_DIR}/sdk/include")
    target_compile_definitions(vip_bindings_as_vip PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
    set(AS_VIP_VIP_BINDING_OBJECTS "$<TARGET_OBJECTS:vip_bindings_as_vip>")
else()
    message(FATAL_ERROR "as_vip needs the generated vip consumer bindings (plugins/vip/include/external/plugify)")
endif()

# --- plugin -------------------------------------------------------------------
add_library(plugify-as_vip SHARED
        src/plugin.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
        ${AS_VIP_ADMIN_BINDING_OBJECTS}
        ${AS_VIP_VIP_BINDING_OBJECTS}
)
target_include_directories(plugify-as_vip PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated admin_system/vip/s2sdk/plg headers and the wrappers must
# not fail -Werror on our sources (src/plugin.cpp stays warning-clean).
target_include_directories(plugify-as_vip SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/plugins/admin_system/include"
        "${CMAKE_SOURCE_DIR}/plugins/admin_system/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/sdk/include"
        "${CMAKE_SOURCE_DIR}/libs/pisexcompat/include")
target_link_libraries(plugify-as_vip PRIVATE
        pisexcompat
        -static-libstdc++ -static-libgcc
)
target_compile_options(plugify-as_vip PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(plugify-as_vip PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="as_vip"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
)
set_target_properties(plugify-as_vip PROPERTIES OUTPUT_NAME as_vip)

if(UNIX AND NOT APPLE)
    target_link_options(plugify-as_vip PRIVATE
            "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/sym/version_script.lds")
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/as_vip.pplugin.in"
        "${CMAKE_CURRENT_BINARY_DIR}/as_vip.pplugin"
)

# --- tests --------------------------------------------------------------------
# Engine-free: the config map, the group selection and the real production/live vip.ini files.
# Linking pisexcompat pulls the s2sdk/hudmenu binding globals (the test leaves them null; the
# KeyValues parser never calls into s2sdk).
add_executable(as_vip_tests tests/as_vip_tests.cpp)
target_include_directories(as_vip_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(as_vip_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(as_vip_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        AS_VIP_TEST_PROD_CONFIG="${CMAKE_SOURCE_DIR}/ref/prod-27016/addons/configs/admin_system/vip.ini"
        AS_VIP_TEST_LIVE_CONFIG="${CMAKE_SOURCE_DIR}/ref/pisex/live-configs-admin_system/vip.ini")
target_link_libraries(as_vip_tests PRIVATE pisexcompat -static-libstdc++ -static-libgcc)
set_target_properties(as_vip_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
