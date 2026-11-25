# Injected with CMAKE_PROJECT_INCLUDE by mayhem/build.sh. core/build/FindMySOFA.cmake points
# ZLIB_INCLUDE_DIR at core/deps/zlib/include (upstream's get_dependencies.py layout), which does not
# exist here; zlib comes from the base image instead. Defining ZLIB::ZLIB first makes FindZLIB reuse
# this target rather than one carrying the missing include path.
if (NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB STATIC IMPORTED GLOBAL)
    set_target_properties(ZLIB::ZLIB PROPERTIES IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libz.a)
endif()
