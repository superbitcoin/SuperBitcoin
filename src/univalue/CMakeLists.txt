file(GLOB sources "*.cpp")
file(GLOB headers "*.h")


list(APPEND sources "lib/univalue.cpp"  "lib/univalue_read.cpp"  "lib/univalue_write.cpp")
list(APPEND headers "include/univalue.h")
list(APPEND headers "lib/univalue_escapes.h" "lib/univalue_utffilter.h")

add_library(univalue ${sources} ${headers})


