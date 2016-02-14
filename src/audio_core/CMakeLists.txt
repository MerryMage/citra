set(SRCS
            audio_core.cpp
            hle/dsp.cpp
            hle/pipe.cpp
            soundio_sink.cpp
            )

set(HEADERS
            audio_core.h
            hle/dsp.h
            hle/pipe.h
            sink.h
            soundio_sink.h
            )

create_directory_groups(${SRCS} ${HEADERS})

include_directories(${SOUNDIO_INCLUDE_DIR})

add_library(audio_core STATIC ${SRCS} ${HEADERS})