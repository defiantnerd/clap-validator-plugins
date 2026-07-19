# Conformance test: run free-audio/clap-validator against the built binary.
# Skipped (not failed) when clap-validator is not installed.
find_program(CLAP_VALIDATOR clap-validator)

if(CLAP_VALIDATOR)
    if(APPLE)
        set(plugin_path "$<TARGET_BUNDLE_DIR:clap-validator-plugins>")
    else()
        set(plugin_path "$<TARGET_FILE:clap-validator-plugins>")
    endif()
    add_test(NAME clap-validator-conformance
             COMMAND ${CLAP_VALIDATOR} validate "${plugin_path}")
else()
    message(STATUS "clap-validator not found — conformance test disabled. "
                   "Install it from https://github.com/free-audio/clap-validator "
                   "or pass -DCLAP_VALIDATOR=/path/to/clap-validator.")
endif()
