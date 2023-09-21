#include <assert.h>
#include <string.h>

#include "conn_slots.h"
#include "process_data.h"

conn_slot_t conn_slots[ MAX_CONN_SLOT ];


void init_conn_slots()
{
    for( int i = 0; i < MAX_CONN_SLOT; i++)
    {
        conn_slots[ i ].idstr[ 0 ] = 0;
        conn_slots[ i ].filedes = -1;
        conn_slots[ i ].last_number = 0;
        conn_slots[ i ].last_time = 0;
        conn_slots[ i ].tid = -1;
    }
}


int search_connection( const char *idstr )
{
    for( int i = 0; i < MAX_CONN_SLOT; i++)
        if( strcmp( idstr, conn_slots[ i ].idstr) == 0 )
            return i;
    return -1;
}


int get_filedes( int idx )
{
    assert( idx >= 0 && idx < MAX_CONN_SLOT );
    return conn_slots[ idx ].filedes;
}


void set_filedes( int idx, int filedes)
{
    assert( idx >= 0 && idx < MAX_CONN_SLOT );
    conn_slots[ idx ].filedes = filedes;
}


int find_free_connection_slot()
{
    for( int i = 0; i < MAX_CONN_SLOT; i++)
        if( conn_slots[ i ].idstr[ 0 ] == 0 )
            return i;
    return -1;
}


void add_connection( int idx, const char *idstr, int filedes)
{
    assert( idx >=0 && idx < MAX_CONN_SLOT );
    assert( conn_slots[ idx ].idstr[ 0 ] == 0 );
    
    strcpy( conn_slots[ idx ].idstr, idstr);
    conn_slots[ idx ].filedes = filedes;
    conn_slots[ idx ].last_number = 0;
    conn_slots[ idx ].last_time = 0;
    conn_slots[ idx ].tid = pthread_self();
}


void remove_connection( int filedes )
{
    for( int i = 0; i < MAX_CONN_SLOT; i++)
    {
        if( conn_slots[ i ].filedes == filedes )
        {
            slog( "removing connection for %s\n", conn_slots[ i ].idstr);
            conn_slots[ i ].idstr[ 0 ] = 0;
            conn_slots[ i ].filedes = -1;
            conn_slots[ i ].last_number = 0;
            conn_slots[ i ].last_time = 0;
            conn_slots[ i ].tid = -1;
        }
    }
}


void slog_connections()
{
    slog( "Current connections:\n" );
    for( int i = 0; i < MAX_CONN_SLOT; i++)
    {
        if( conn_slots[ i ].idstr[ 0 ] )
            slog( "%s  %d\n", conn_slots[ i ].idstr, conn_slots[ i ].filedes);
    }
}
