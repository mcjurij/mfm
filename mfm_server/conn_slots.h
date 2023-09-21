#ifndef _CONN_SLOTS_H_
#define _CONN_SLOTS_H_

#include <stdint.h>
#include <pthread.h>

#include "conf.h"

typedef struct
{
    char      idstr[32];
    int       filedes;
    uint32_t  last_number;
    uint64_t  last_time;
    pthread_t tid;
} conn_slot_t;


extern conn_slot_t conn_slots[ MAX_CONN_SLOT ];

void init_conn_slots();
int search_connection( const char *idstr );
int get_filedes( int idx );
void set_filedes( int idx, int filedes);
int find_free_connection_slot();
void add_connection( int idx, const char *idstr, int filedes);

void remove_connection( int filedes );
void slog_connections();


#endif
