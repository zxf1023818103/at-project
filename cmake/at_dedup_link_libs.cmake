include_guard()

# at_dedup_link_libs(<target> <output_var>)
#
# Walks the transitive INTERFACE_LINK_LIBRARIES of <target>, collects
# every unique CMake target that contributes a real static archive to
# the final link, and returns the deduplicated list in <output_var>
# (PARENT_SCOPE).
#
# Why this exists: CMake < 3.29 has no built-in link-line de-duplication
# for cascading PUBLIC INTERFACE deps. Our umbrellas (bouffalo_sdk,
# wifi4, bflb_zypher_bluetooth, …) each PUBLIC-link a bunch of STATIC
# libs; the same lib (libbl602_std.a, libbflb_lhal.a, libcjson.a, …)
# ends up on the link line 2–3×. Lazy linking ignored dups; under
# -Wl,--whole-archive (which component/bouffalo_sdk/bflb_image_tool/CMakeLists.txt's
# bflb_add_executable wraps the lib list in to match the legacy SDK)
# every duplicate force-includes the archive again, multiplying the
# LDS-KEEP'd .vector / .init sections and overflowing the .init
# budget. CMP0156 would fix this cleanly; we're on 3.28 (the policy
# arrived in 3.29+), hence this helper.
#
# What's *kept* in the output:
#   - STATIC library targets (have an actual .a to whole-archive)
#   - INTERFACE library targets whose own INTERFACE_LINK_LIBRARIES
#     resolves to STATIC entries (they're traversed; their .a-producing
#     descendants land in the output)
#   - Plain file paths (e.g. /…/libfirmware.a) and -l<name> entries
#
# What's *dropped*:
#   - the root <target> itself
#   - INTERFACE libraries (no archive — wrapping them in --whole-archive
#     is a harmless no-op but pollutes the link line)
#   - OBJECT libraries (their .obj files go in via $<TARGET_OBJECTS:…>,
#     not as a separate archive entry)
#   - generator expressions ($<…>) — leave for caller to handle
#   - -Wl,… / -W… flags — those belong in LINK_OPTIONS, not LINK_LIBRARIES

function(at_dedup_link_libs target output_var)
    set(_queue "${target}")
    set(_seen "")
    set(_libs "")

    while(_queue)
        list(POP_FRONT _queue _curr)
        if("${_curr}" IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen "${_curr}")

        if(TARGET "${_curr}")
            get_target_property(_type "${_curr}" TYPE)

            # Enqueue children (interface link libs) before deciding whether
            # to emit the current target — we always want to walk through
            # INTERFACEs to reach their concrete .a-producing descendants.
            get_target_property(_deps "${_curr}" INTERFACE_LINK_LIBRARIES)
            if(_deps)
                foreach(_dep IN LISTS _deps)
                    # Skip CMake generator expressions and linker flags —
                    # they aren't libraries we can wrap in --whole-archive.
                    if(_dep MATCHES "^\\$<")
                        continue()
                    endif()
                    if(_dep MATCHES "^-")
                        continue()
                    endif()
                    list(APPEND _queue "${_dep}")
                endforeach()
            endif()

            # Skip the root, INTERFACE-only libs (no archive), and OBJECT
            # libs (their objects are already in the link via TARGET_OBJECTS
            # propagation set up by the umbrella).
            if("${_curr}" STREQUAL "${target}")
                continue()
            endif()
            if("${_type}" STREQUAL "INTERFACE_LIBRARY")
                continue()
            endif()
            if("${_type}" STREQUAL "OBJECT_LIBRARY")
                continue()
            endif()

            list(APPEND _libs "${_curr}")
        elseif(EXISTS "${_curr}")
            # Plain file path (e.g. a prebuilt .a referenced by absolute
            # path from an INTERFACE wrapper like wifi4_firmware).
            list(APPEND _libs "${_curr}")
        elseif(_curr MATCHES "^-l")
            # System library spec — keep it.
            list(APPEND _libs "${_curr}")
        endif()
    endwhile()

    set("${output_var}" "${_libs}" PARENT_SCOPE)
endfunction()
