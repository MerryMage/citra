set(SRCS
            emu_window/emu_window_glfw.cpp
            citra.cpp
            config.cpp
            citra.rc
            )
set(HEADERS
            emu_window/emu_window_glfw.h
            config.h
            default_ini.h
            resource.h
            )

create_directory_groups(${SRCS} ${HEADERS})

include_directories(${GLFW_INCLUDE_DIRS})
link_directories(${GLFW_LIBRARY_DIRS})

add_executable(citra ${SRCS} ${HEADERS})
target_link_libraries(citra core video_core audio_core common)
target_link_libraries(citra ${GLFW_LIBRARIES} ${OPENGL_gl_LIBRARY} ${SOUNDIO_LIBRARY} inih glad)
if (MSVC)
    target_link_libraries(citra getopt)
endif()
target_link_libraries(citra ${PLATFORM_LIBRARIES})

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux|FreeBSD|OpenBSD|NetBSD")
    install(TARGETS citra RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif()

if (CITRA_USE_BUNDLED_SOUNDIO AND MSVC)
    set(DLL_DEST "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/")

    string(REPLACE "/" "\\\\" DLL_DEST ${DLL_DEST})
    string(REPLACE "/" "\\\\" SOUNDIO_DLL_DIR ${SOUNDIO_DLL_DIR})

    # /NJH /NJS /NDL /NFL /NC /NS /NP - Silence any output
    # cmake adds an extra check for command success which doesn't work too well with robocopy
    # so trick it into thinking the command was successful with the || cmd /c "exit /b 0"
    add_custom_command(TARGET citra POST_BUILD
        COMMAND robocopy ${SOUNDIO_DLL_DIR} ${DLL_DEST} libsoundio.dll /NJH /NJS /NDL /NFL /NC /NS /NP || cmd /c "exit /b 0"
    )

    unset(DLL_DEST)
endif()
