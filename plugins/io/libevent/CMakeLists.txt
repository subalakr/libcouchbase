INSTALL(
    FILES
    libevent_io_opts.h
    DESTINATION
    include/libcouchbase)


IF(NOT (HAVE_LIBEVENT OR HAVE_LIBEVENT2))
    RETURN()
ENDIF()

IF(HAVE_LIBEVENT2)
    ADD_DEFINITIONS(-DHAVE_LIBEVENT2)
    INCLUDE_DIRECTORIES(AFTER ${LIBEVENT_INCLUDE_DIR})
ELSE()
    INCLUDE_DIRECTORIES(AFTER ${LIBEVENT_INCLUDE_DIR})
    ADD_DEFINITIONS(-DHAVE_LIBEVENT)
ENDIF()

ADD_LIBRARY(couchbase_libevent SHARED plugin-libevent.c)
SET_TARGET_PROPERTIES(couchbase_libevent
    PROPERTIES
    PREFIX "lib"
    IMPORT_PREFIX "lib"
    COMPILE_FLAGS "${CMAKE_C_FLAGS} ${LCB_CORE_CFLAGS}")
TARGET_LINK_LIBRARIES(couchbase_libevent ${LIBEVENT_LIBRARIES})
INSTALL(TARGETS
    couchbase_libevent
    LIBRARY DESTINATION lib)
