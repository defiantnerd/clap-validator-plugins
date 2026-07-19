# Adds a `copy-to-clap-folder` convenience target that installs the built
# .clap into the platform's per-user CLAP search path. With
# CVP_COPY_AFTER_BUILD=ON the copy also runs automatically after each build.
function(cvp_add_copy_target target)
    if(APPLE)
        set(dest "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
        set(copy_cmd ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:${target}>" "${dest}/$<TARGET_BUNDLE_DIR_NAME:${target}>")
    elseif(WIN32)
        set(dest "$ENV{LOCALAPPDATA}/Programs/Common/CLAP")
        set(copy_cmd ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${dest}/")
    else()
        set(dest "$ENV{HOME}/.clap")
        set(copy_cmd ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${dest}/")
    endif()

    add_custom_target(copy-to-clap-folder
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dest}"
        COMMAND ${copy_cmd}
        DEPENDS ${target}
        COMMENT "Copying $<TARGET_FILE_NAME:${target}> to ${dest}")

    if(CVP_COPY_AFTER_BUILD)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${dest}"
            COMMAND ${copy_cmd})
    endif()
endfunction()
