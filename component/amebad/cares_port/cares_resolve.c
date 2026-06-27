/* cares_resolve.c — synchronous c-ares resolver over the FreeRTOS+TCP BSD-sockets shim, driven by
 * a select() event loop. See cares_resolve.h. */
#include <string.h>
#include <stdio.h>

/* BSD socket types FIRST: struct sockaddr_in must be fully defined before FreeRTOS_Sockets.h's
 * `#define sin_addr sin_address.ulIP_IPv4` backward-compat alias, which would otherwise rewrite the
 * BSD struct's sin_addr field into a syntax error. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include "ares.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_Routing.h"
#undef sin_addr   /* drop the FreeRTOS IPv4-compat alias; we read sin_address.ulIP_IPv4 directly */

#include "cares_resolve.h"
#include "at_log.h"

#ifndef AF_UNSPEC
#define AF_UNSPEC 0
#endif

static SemaphoreHandle_t  s_lock;
static int                s_lib_inited;
static char               s_servers_csv[ 96 ];   /* DNS servers; applied to each transient channel */

struct gai_ctx {
    int                        family;
    struct freertos_sockaddr * out;
    volatile int               done;
    volatile int               ok;
    volatile int               status;
};

static void prv_gai_cb( void * arg, int status, int timeouts, struct ares_addrinfo * res )
{
    struct gai_ctx * ctx = ( struct gai_ctx * ) arg;

    ( void ) timeouts;
    ctx->status = status;

    if( ( status == ARES_SUCCESS ) && ( res != NULL ) )
    {
        struct ares_addrinfo_node * n;

        for( n = res->nodes; n != NULL; n = n->ai_next )
        {
            if( ( n->ai_family == AF_INET ) &&
                ( ( ctx->family == AF_INET ) || ( ctx->family == AF_UNSPEC ) ) )
            {
                struct sockaddr_in * si = ( struct sockaddr_in * ) n->ai_addr;

                ( void ) memset( ctx->out, 0, sizeof( *ctx->out ) );
                ctx->out->sin_family = FREERTOS_AF_INET;
                ctx->out->sin_len    = ( uint8_t ) sizeof( *ctx->out );
                ctx->out->sin_address.ulIP_IPv4 = si->sin_addr.s_addr;   /* network order */
                ctx->ok = 1;
                break;
            }
            if( ( n->ai_family == AF_INET6 ) &&
                ( ( ctx->family == AF_INET6 ) || ( ctx->family == AF_UNSPEC ) ) )
            {
                struct sockaddr_in6 * si6 = ( struct sockaddr_in6 * ) n->ai_addr;

                ( void ) memset( ctx->out, 0, sizeof( *ctx->out ) );
                ctx->out->sin_family = FREERTOS_AF_INET6;
                ctx->out->sin_len    = ( uint8_t ) sizeof( *ctx->out );
                ( void ) memcpy( ctx->out->sin_address.xIP_IPv6.ucBytes, si6->sin6_addr.s6_addr, 16 );
                ctx->ok = 1;
                break;
            }
        }
    }

    if( res != NULL )
    {
        ares_freeaddrinfo( res );
    }
    ctx->done = 1;
}

void cares_resolver_init( void )
{
    if( s_lock == NULL )
    {
        s_lock = xSemaphoreCreateMutex();
    }
    if( !s_lib_inited )
    {
        ( void ) ares_library_init( ARES_LIB_INIT_ALL );
        s_lib_inited = 1;
        log_info( "[cares] ready" );
    }
}

/* Cache the current DNS servers (DHCP-learned + the DNS_SERVER env override) as a CSV; cares_resolve
 * re-applies it to the short-lived channel it builds per query. No channel is kept alive here. */
void cares_resolver_set_servers( void )
{
    NetworkEndPoint_t * ep;
    unsigned            i;

    s_servers_csv[ 0 ] = '\0';
    for( ep = FreeRTOS_FirstEndPoint( NULL ); ep != NULL; ep = FreeRTOS_NextEndPoint( NULL, ep ) )
    {
        for( i = 0; i < ipconfigENDPOINT_DNS_ADDRESS_COUNT; i++ )
        {
            uint32_t d = ep->ipv4_settings.ulDNSServerAddresses[ i ];

            if( d != 0U )
            {
                char b[ 24 ];

                ( void ) snprintf( b, sizeof( b ), "%u.%u.%u.%u",
                                   ( unsigned ) ( d & 0xffU ),         ( unsigned ) ( ( d >> 8 ) & 0xffU ),
                                   ( unsigned ) ( ( d >> 16 ) & 0xffU ), ( unsigned ) ( ( d >> 24 ) & 0xffU ) );
                if( ( strlen( s_servers_csv ) + strlen( b ) + 2U ) < sizeof( s_servers_csv ) )
                {
                    if( s_servers_csv[ 0 ] != '\0' )
                    {
                        ( void ) strcat( s_servers_csv, "," );
                    }
                    ( void ) strcat( s_servers_csv, b );
                }
            }
        }
    }
    log_info( "[cares] servers=%s", s_servers_csv );
}

int cares_resolve( const char * host, int family, struct freertos_sockaddr * out )
{
    struct ares_options        opts;
    struct gai_ctx             ctx;
    struct ares_addrinfo_hints hints;
    ares_channel_t           * channel = NULL;
    int                        mask = 0;
    int                        rc;

    if( !s_lib_inited || ( host == NULL ) || ( out == NULL ) )
    {
        return -1;
    }

    if( s_lock != NULL )
    {
        ( void ) xSemaphoreTake( s_lock, portMAX_DELAY );
    }

    /* Build a fresh channel per resolve and tear it down at the end. Keeping a channel alive holds
     * its htables + rand state + the kept-warm UDP connection's in_buf on the heap (~12 KB) between
     * the infrequent connect/reconnect resolves; create-on-demand keeps the steady-state cost ~0. */
    ( void ) memset( &opts, 0, sizeof( opts ) );
    opts.timeout = 3000;  mask |= ARES_OPT_TIMEOUTMS;                          /* 3 s per try */
    opts.tries   = 2;     mask |= ARES_OPT_TRIES;
    opts.flags   = ARES_FLAG_NOSEARCH | ARES_FLAG_NOALIASES;  mask |= ARES_OPT_FLAGS;
    opts.lookups = "b";   mask |= ARES_OPT_LOOKUPS;                            /* DNS only, no hosts file */

    if( ares_init_options( &channel, &opts, mask ) != ARES_SUCCESS )
    {
        log_error( "[cares] ares_init_options failed" );
        if( s_lock != NULL ) { ( void ) xSemaphoreGive( s_lock ); }
        return -1;
    }
    if( s_servers_csv[ 0 ] != '\0' )
    {
        ( void ) ares_set_servers_csv( channel, s_servers_csv );
    }

    ctx.family = family;
    ctx.out    = out;
    ctx.done   = 0;
    ctx.ok     = 0;
    ctx.status = -1;

    ( void ) memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = family;
    hints.ai_flags  = ARES_AI_NOSORT;

    ares_getaddrinfo( channel, host, NULL, &hints, prv_gai_cb, &ctx );

    /* Drive the query to completion: select() on c-ares' fds, then ares_process() (which also
     * services its own retry/timeout schedule when select returns 0). The callback sets ctx.done.
     * iters is a safety bound (c-ares' own ARES_OPT_TIMEOUTMS*tries normally ends it well first). */
    {
        int iters = 0;
        while( ( ctx.done == 0 ) && ( iters < 40 ) )
        {
            fd_set           rf, wf;
            struct timeval   tv, maxtv;
            struct timeval * tvp;
            int              nfds, r;

            FD_ZERO( &rf );
            FD_ZERO( &wf );
            nfds = ares_fds( channel, &rf, &wf );

            maxtv.tv_sec  = 2;
            maxtv.tv_usec = 0;
            tvp = ares_timeout( channel, &maxtv, &tv );

            r = select( nfds, &rf, &wf, NULL, tvp );
            if( r < 0 )
            {
                break;
            }
            ares_process( channel, &rf, &wf );
            iters++;
        }
    }

    rc = ctx.ok ? 0 : -1;
    log_info( "[cares] resolve %s fam=%d status=%d -> %s", host, family, ctx.status,
              ( rc == 0 ) ? "ok" : "fail" );

    /* Tears down sockets + buffers + htables; also fires any still-pending query callback
     * synchronously with ARES_EDESTRUCTION (ctx is still in scope here, so that is safe). */
    ares_destroy( channel );

    if( s_lock != NULL )
    {
        ( void ) xSemaphoreGive( s_lock );
    }
    return rc;
}
