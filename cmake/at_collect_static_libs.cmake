include_guard()

# Walk a target's transitive link dependencies and collect every node whose
# TYPE is STATIC_LIBRARY. Resolves each to its $<TARGET_FILE:...> (absolute
# .a path). Plain-string entries (-lm, pthread, etc.) and interface-only /
# shared / object targets are skipped.
#
# Usage:
#   at_collect_static_libs(at_user OUT)
#   message("${OUT}")  # → list of $<TARGET_FILE:...> generator expressions
#
# Pair with file(GENERATE) to write a Make-readable link list, e.g.:
#   file(GENERATE OUTPUT user_link.list CONTENT "$<JOIN:${OUT},\n>\n")
# AmebaD's Make-driven final linker step reads user_link.list at link time.
function(at_collect_static_libs target out_var)
    set(_result "")
    set(_visited "")
    set(_pending "${target}")
    while(_pending)
        list(POP_FRONT _pending _cur)
        if(_cur IN_LIST _visited)
            continue()
        endif()
        list(APPEND _visited "${_cur}")
        if(NOT TARGET "${_cur}")
            continue()  # plain string like "-lm" or "pthread"
        endif()
        get_target_property(_type "${_cur}" TYPE)
        if(_type STREQUAL "STATIC_LIBRARY")
            list(APPEND _result "$<TARGET_FILE:${_cur}>")
        endif()
        get_target_property(_deps "${_cur}" LINK_LIBRARIES)
        if(_deps)
            list(APPEND _pending ${_deps})
        endif()
        get_target_property(_idep "${_cur}" INTERFACE_LINK_LIBRARIES)
        if(_idep)
            list(APPEND _pending ${_idep})
        endif()
    endwhile()
    set("${out_var}" "${_result}" PARENT_SCOPE)
endfunction()
