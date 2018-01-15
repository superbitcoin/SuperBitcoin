file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

set(depSources
        ../crypto/sha1.cpp
        ../crypto/sha1.h
        )

add_library(script ${sources} ${headers} ${depSources})


