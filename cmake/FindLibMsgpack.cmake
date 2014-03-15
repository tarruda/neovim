# - Try to find msgpack
# Once done, this will define
#
#  LibMsgpack_FOUND - system has msgpack
#  LibMsgpack_INCLUDE_DIRS - the msgpack include directories
#  LibMsgpack_LIBRARIES - link these to use msgpack
#
# Set the LibMsgpack_USE_STATIC variable to specify if static libraries should
# be preferred to shared ones.

include(LibFindMacros)

# Include dir
find_path(LibMsgpack_INCLUDE_DIR
    NAMES msgpack.h
)

set(_msgpack_names msgpack)

# If we're asked to use static linkage, add libmsgpack.a as a preferred library name.
if(LibMsgpack_USE_STATIC)
    list(INSERT _msgpack_names 0 libmsgpack.a)
endif()

# The library itself. Note that we prefer the static version.
find_library(LibMsgpack_LIBRARY
    NAMES ${_msgpack_names}
)

# Set the include dir variables and the libraries and let libfind_process do the rest.
# NOTE: Singular variables for this library, plural for libraries this this lib depends on.
set(LibMsgpack_PROCESS_INCLUDES LibMsgpack_INCLUDE_DIR)
set(LibMsgpack_PROCESS_LIBS LibMsgpack_LIBRARY)
libfind_process(LibMsgpack)
