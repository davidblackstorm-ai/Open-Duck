# JUCE's Windows .rc custom command must be invalidated when Info.txt changes.
# Include this file and call the function in the SAME directory scope as
# juce_add_plugin()/juce_add_console_app(), AFTER that JUCE target is created.
# This adds a dependency to JUCE's existing command without changing JUCE itself.
function(openduck_fix_windows_resource_dependencies target)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        return()
    endif()

    if(NOT TARGET "${target}" OR NOT TARGET "${target}_rc_lib")
        message(FATAL_ERROR "Windows RC dependency fix requires an existing JUCE Windows target: ${target}")
    endif()

    get_target_property(_odx_target_source_dir "${target}" SOURCE_DIR)
    if(NOT _odx_target_source_dir STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        message(FATAL_ERROR "Call the Windows RC dependency fix in the directory that created ${target}")
    endif()

    get_target_property(_odx_generated_dir "${target}" JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(_odx_info_file "${target}" JUCE_INFO_FILE)
    if(NOT _odx_generated_dir OR NOT IS_ABSOLUTE "${_odx_generated_dir}"
       OR NOT _odx_info_file OR NOT IS_ABSOLUTE "${_odx_info_file}"
       OR NOT EXISTS "${_odx_info_file}")
        message(FATAL_ERROR "JUCE Windows RC properties are missing or invalid for ${target}")
    endif()

    # APPEND only accepts the options being appended. In particular, do not
    # repeat VERBATIM/BYPRODUCTS from JUCE's original command (CMP0175).
    add_custom_command(
        OUTPUT "${_odx_generated_dir}/${target}_resources.rc"
        APPEND
        DEPENDS "${_odx_info_file}")
endfunction()
