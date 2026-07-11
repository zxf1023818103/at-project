/*
 * dot11d_tally.c — tally / vote / validator half of the 802.11d component. Its OWN translation
 * unit so an image that only needs dot11d_cc_valid (e.g. the bl616 boot default, which has no
 * 80211d_port yet) can pull this archive member without dragging in dot11d.c's strong
 * dot11d_port_* references.
 */
#include <stddef.h>

#include "dot11d.h"

void dot11d_tally_add( dot11d_tally_t * t, char c0, char c1 )
{
    int i;

    t->aps++;
    if( !( ( c0 >= 'A' ) && ( c0 <= 'Z' ) && ( c1 >= 'A' ) && ( c1 <= 'Z' ) ) )
    {
        return;   /* AP advertised no (valid) Country IE */
    }
    for( i = 0; i < t->num; i++ )
    {
        if( ( t->cc[ i ][ 0 ] == c0 ) && ( t->cc[ i ][ 1 ] == c1 ) )
        {
            t->count[ i ]++;
            return;
        }
    }
    if( t->num < ( int ) ( sizeof( t->cc ) / sizeof( t->cc[ 0 ] ) ) )
    {
        t->cc[ t->num ][ 0 ] = c0;
        t->cc[ t->num ][ 1 ] = c1;
        t->count[ t->num ] = 1;
        t->num++;
    }
}

const char * dot11d_tally_best( dot11d_tally_t * t, char out[ 3 ] )
{
    int i, best = -1, best_n = 0;

    for( i = 0; i < t->num; i++ )
    {
        if( t->count[ i ] > best_n )
        {
            best = i;
            best_n = t->count[ i ];
        }
    }
    if( best < 0 )
    {
        return NULL;
    }
    out[ 0 ] = t->cc[ best ][ 0 ];
    out[ 1 ] = t->cc[ best ][ 1 ];
    out[ 2 ] = '\0';
    return out;
}

int dot11d_cc_valid( const char * cc )
{
    return ( cc != NULL ) &&
           ( cc[ 0 ] >= 'A' ) && ( cc[ 0 ] <= 'Z' ) &&
           ( cc[ 1 ] >= 'A' ) && ( cc[ 1 ] <= 'Z' ) &&
           ( cc[ 2 ] == '\0' );
}

