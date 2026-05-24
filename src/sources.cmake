set(AT_USER_SRC_DIR ${CMAKE_CURRENT_LIST_DIR})

# Ragel-generated parser: src/AtCommandParser.rl -> <bindir>/AtCommandParser.rl.cpp.
# .rl.cpp is in src/.gitignore (never tracked); regenerated per-build dir so each
# chip / platform build gets its own copy and stale files in src/ are ignored.
find_program(RAGEL_EXECUTABLE ragel REQUIRED)
set(_at_ragel_out ${CMAKE_CURRENT_BINARY_DIR}/AtCommandParser.rl.cpp)
add_custom_command(
    OUTPUT  ${_at_ragel_out}
    COMMAND ${RAGEL_EXECUTABLE} -G2 -o ${_at_ragel_out} ${AT_USER_SRC_DIR}/AtCommandParser.rl
    DEPENDS ${AT_USER_SRC_DIR}/AtCommandParser.rl
    COMMENT "ragel -G2 AtCommandParser.rl -> ${_at_ragel_out}"
    VERBATIM
)

file(GLOB AT_USER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    LIST_DIRECTORIES false)
# Drop any stale src/AtCommandParser.rl.cpp picked up by the glob — we want the
# build-dir copy to be the only one.
list(FILTER AT_USER_SOURCES EXCLUDE REGEX "/AtCommandParser\\.rl\\.cpp$")
list(APPEND AT_USER_SOURCES ${_at_ragel_out})
