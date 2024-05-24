# core

set(TARGET ggmorse)

add_library(${TARGET}
    ggmorse.cpp
    resampler.cpp
    )

target_include_directories(${TARGET} PUBLIC
    .
    ../include
    )

if (BUILD_SHARED_LIBS)
    target_link_libraries(${TARGET} PUBLIC
        ${CMAKE_DL_LIBS}
        )

    target_compile_definitions(${TARGET} PUBLIC
        GGMORSE_SHARED
        )
    if (MSVC)
        target_compile_definitions(${TARGET} PUBLIC
            GGMORSE_BUILD
            )
    endif()
endif()

if (MINGW)
    target_link_libraries(${TARGET} PUBLIC
        stdc++
        )
endif()

set_target_properties(${TARGET} PROPERTIES PUBLIC_HEADER ../include/ggmorse/ggmorse.h)

install(TARGETS ${TARGET}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/static
    PUBLIC_HEADER DESTINATION  include/${PROJECT_NAME}
    )
