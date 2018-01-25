file(GLOB sources "*.cpp" "crypto/*.cpp" "crypto/allocators/*.cpp" "crypto/ctaes/*.c" "net/*.cpp"  )
file(GLOB headers "*.h" "crypto/*.h" "crypto/allocators/*.h" "crypto/ctaes/*.h" "net/*.h")

add_library(utils ${sources} ${headers})
