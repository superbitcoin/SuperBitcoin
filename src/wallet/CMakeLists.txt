file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

set(depSources
        ../crypto/aes.h
        ../crypto/aes.cpp
        )

add_library(wallet ${sources} ${headers} ${depSources} ${LIBDB_CXX_LIBRARIES})
