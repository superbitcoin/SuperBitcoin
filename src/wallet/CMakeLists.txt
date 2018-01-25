file(GLOB sources "*.cpp")
file(GLOB headers "*.h")
link_directories(../utils)
set(depSources
        ../utils/crypto/aes.h
        ../utils/crypto/aes.cpp
        )

add_library(wallet ${sources} ${headers} ${depSources} ${LIBDB_CXX_LIBRARIES})
