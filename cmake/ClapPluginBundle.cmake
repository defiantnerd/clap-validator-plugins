# Configures a MODULE library target to be packaged as a .clap plugin
# for the current platform (macOS bundle, Windows/Linux renamed module).
function(cvp_configure_clap_target target)
    if(APPLE)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION clap
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.clap-validator.plugins"
            MACOSX_BUNDLE_BUNDLE_NAME "CLAP Validator Plugins"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
        # Ad-hoc signature: unsigned bundles are refused by the loader on Apple Silicon.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:${target}>")
    else()
        set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".clap")
    endif()
endfunction()
