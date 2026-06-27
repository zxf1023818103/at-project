/* cares_stubs.c — no-op definitions for c-ares symbols whose real implementations live in source
 * files we deliberately do not compile:
 *   - ares_sortaddrinfo (RFC 6724 address sorting): we always resolve with ARES_AI_NOSORT, so the
 *     sorter is never reached; the real one also needs getifaddrs + IN6_IS_ADDR_* macros we lack.
 *   - ares_is_onion_domain: lives in ares_getnameinfo.c (excluded); we don't special-case .onion.
 *   - the optional event thread (ares_event_thread_* / ares_event_configchg_*): we drive
 *     ares_process() synchronously over select(), never the libuv-style background thread, so these
 *     init/destroy hooks are no-ops (avoids pulling pthread/pipe + the epoll/kqueue backends).
 */
#include "ares.h"
#include "ares_private.h"
#include "ares_event.h"

ares_status_t ares_sortaddrinfo( ares_channel_t * channel, struct ares_addrinfo_node * ai_node )
{
    ( void ) channel;
    ( void ) ai_node;
    return ARES_SUCCESS;
}

ares_bool_t ares_is_onion_domain( const char * name )
{
    ( void ) name;
    return ARES_FALSE;
}

ares_status_t ares_event_thread_init( ares_channel_t * channel )
{
    ( void ) channel;
    return ARES_SUCCESS;
}

void ares_event_thread_destroy( ares_channel_t * channel )
{
    ( void ) channel;
}

ares_status_t ares_event_configchg_init( ares_event_configchg_t ** configchg,
                                         ares_event_thread_t     * e )
{
    ( void ) configchg;
    ( void ) e;
    return ARES_SUCCESS;
}

void ares_event_configchg_destroy( ares_event_configchg_t * configchg )
{
    ( void ) configchg;
}
