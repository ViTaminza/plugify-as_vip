# as_vip — Plugify C++ plugin: port of the Pisex admin_system module "VIP".
#
#   plugify-as_vip  the plugin shared library (entry "bin/as_vip" -> bin/libas_vip.so)
#
# Self-contained (docs/PLUGIN_STANDARD.md §7): no pisexcompat, no s2guard and no s2sdk calls, and
# no hudmenu use. The module is a consumer of exactly two Plugify APIs:
#   * the admin_system core (admin_api.hpp: HasPermission / OnAdminConnected),
#   * the vip core (vip_api.hpp: VIP_GiveClientVIP).
# It compiles the generated binding globals of those two cores into its own .so:
#   __admin_system_*  (plugins/admin_system/include/external/plugify)
#   __vip_*           (plugins/vip/include/external/plugify)
# and exports them (sym/version_script.lds) so the plugify module fills them from the dependencies
# declared in the manifest. The vip.ini "VIP" table is parsed by the header-only KV1 reader in
# src/vip_groups.hpp, so neither s2sdk_bindings nor hudmenu_bindings is linked. Nothing else is
# linked beyond plugify_sdk.

set(AS_VIP_VERSION "1.1.0")

# --- generated consumer bindings --------------------------------------------------
# admin_system_bindings_obj (plugins/admin_system) and vip_bindings (plugins/vip) exist when their
# directories configure first. The top level globs and sorts plugins/*, so admin_system ("a" <
# "as_vip") is configured before this directory but vip is not ("as_vip" < "vip"); an exported
# standalone repo carries only the include/ trees of the consumed plugins and no CMakeLists at all.
# A missing target is therefore replaced by a private OBJECT library over the same checked-in
# generated .cpp (the lr_vip_multi / restricted_weapons_vip pattern). The fallbacks get their own
# names: plugins/vip creates vip_bindings unconditionally when it configures later, so reusing that
# name here would be a duplicate target.
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
        ${AS_VIP_ADMIN_BINDING_OBJECTS}
        ${AS_VIP_VIP_BINDING_OBJECTS}
)
target_include_directories(plugify-as_vip PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated admin_system/vip/plg headers and the wrappers must not fail
# -Werror on our sources (src/plugin.cpp stays warning-clean).
target_include_directories(plugify-as_vip SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/plugins/admin_system/include"
        "${CMAKE_SOURCE_DIR}/plugins/admin_system/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(plugify-as_vip PRIVATE
        plugify_sdk
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
# Engine-free and binding-free: only the header-only half of the module (vip_groups.hpp) - the
# self-contained KV1 reader, the candidate config paths and the group selection - on the shipped
# vip.ini files. Nothing here links pisexcompat, plugify_sdk or any engine binding.
#
# The top-level CMakeLists declares the PLUGIFY_PORT_TESTS option *after* the plugin directories,
# so on a first configure it is not defined here yet: default it to the same ON. An explicit
# -DPLUGIFY_PORT_TESTS=OFF (the exported standalone build) is already in the cache and still wins.
if(NOT DEFINED PLUGIFY_PORT_TESTS)
    set(PLUGIFY_PORT_TESTS ON)
endif()
if(PLUGIFY_PORT_TESTS)
    add_executable(as_vip_tests tests/as_vip_tests.cpp)
    target_include_directories(as_vip_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_options(as_vip_tests PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(as_vip_tests PRIVATE
            _GLIBCXX_USE_CXX11_ABI=1
            AS_VIP_TEST_PROD_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/vip_prod.ini"
            AS_VIP_TEST_LIVE_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/vip_live.ini")
    target_link_libraries(as_vip_tests PRIVATE -static-libstdc++ -static-libgcc)
    set_target_properties(as_vip_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
endif()
