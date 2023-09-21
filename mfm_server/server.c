// Open port for PicoW tcp client:
// firewall-cmd --add-port=4200/tcp
// firewall-cmd --runtime-to-permanent

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <ctype.h>
#include <stdbool.h>

#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

#include "proto.h"
#include "conn_slots.h"
#include "process_data.h"
#include "file_mgr.h"

#define ATMEGA_FREQ 10000000

static void dump_data( const uint8_t *buffer, int len, bool print_readable)
{
    for( int i = 0; i < len; i++)
    {
        const char c = buffer[i];
        if( print_readable && isprint(c) )
            printf( " %c", c);
        else
            printf( " %02x", c);
        if( (i & 0xf) == 0 )
            printf( "\n" );
    }
    printf( "\n" );
}


void print_ms( struct timespec *ts )
{
    long            ms; // milliseconds
    time_t          s;  // seconds
    
    s  = ts->tv_sec;
    ms = (long)round( ts->tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds
    if (ms > 999) {
        s++;
        ms = 0;
    }

    printf("Current time: %"PRIdMAX".%03ld seconds since the Epoch\n",
           (intmax_t)s, ms);
}


// -----------------------------------------------------------------------------

typedef struct
{
    char     cmd[4];
    uint32_t data_length;
} message_hdr_t;

typedef struct
{
    char     rply[8];
    uint16_t data_length;
} message_hdr_reply_t;

bool command_switch( message_hdr_t *msg_hdr, int filedes, int conn_idx);


#define PORT    4200

int make_socket( uint16_t port )
{
    int sock;
    struct sockaddr_in name;

    sock = socket( PF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror( "socket" );
        exit( EXIT_FAILURE );
    }

    // bind socket
    name.sin_family = AF_INET;
    name.sin_port = htons(port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if( bind( sock, (struct sockaddr *)&name, sizeof(struct sockaddr_in)) < 0 )
    {
        perror("bind");
        slog( "Can't bind. Exiting.\n" );
        exit( EXIT_FAILURE );
    }
    
    return sock;
}


int read_from_client_2( int filedes, char *buffer, int len)
{
    int nbytes;

    nbytes = read( filedes, buffer, len);
    if (nbytes < 0)
    {
        /* Read error. */
        perror("read_from_client_2(): read");
        slog( "read_from_client_2(): read %s\n", strerror(errno));
        
        return -1;
    }
    else if (nbytes == 0)
    {
        /* End-of-file. */
        printf( "read_from_client_2(): Server: EOF\n" );
        slog( "read_from_client_2(): Server: EOF\n" );
        return -1;
    }
    else
    {
        /* Data read. */
        // printf( "server: got %d bytes.\n", nbytes);
        
        return nbytes;
    }
}


int read_data_from_client( int filedes, message_hdr_t *msg_hdr, uint8_t *data) // read payload
{
    assert( data );
    uint32_t total_bytes = 0;
    const uint32_t chunk_size = 1460*2;
    bool error = false;
    
    while( total_bytes < msg_hdr->data_length && !error )
    {
        int bytes_to_read = (msg_hdr->data_length - total_bytes > chunk_size) ? chunk_size : msg_hdr->data_length - total_bytes;
        // printf( "Waiting for %d bytes...\n", bytes_to_read);
        
        int nbytes = read_from_client_2( filedes, (char *)data + total_bytes, bytes_to_read);
        
        if( nbytes < 0 )  // ? <=0 ?
            error = true;
        else
        {
            total_bytes += nbytes;
            // printf( "received %u total bytes\n", total_bytes);
        }
    }

    if( error )
        return -1;
    else
        return total_bytes;
}


int write_to_client_2( int filedes, const uint8_t *msg, int len)
{
    int nbytes;
    int msglen = len;
    if( msglen == 0 )
        return 0;
    
    nbytes = write( filedes, msg, msglen);
    // printf( "wrote %d bytes\n", nbytes);
    
    if (nbytes < 0)
    {
        perror("write");
        slog( "write_to_client_2(): write %s\n", strerror(errno));
        return -1;
    }
    else if( nbytes == 0 )
    {
        printf( "Server: write failed\n" );
        slog( "write_to_client_2(): write failed\n");
        return -1;
    }
    else
    {
        return nbytes;
    }
}


void *connection_handler( void *arg );

int main( int argc, char *argv[])
{
    int sock;
    struct sockaddr_in clientname;
    socklen_t size;

    init_conn_slots();
    build_protos();
    
    tzset();
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    print_ms( &spec );
    
    slog( "server started\n" );

    if( argc == 2 )
    {
        // argument is "offical" grid time offset in seconds
        // for e.g. from https://www.swissgrid.ch/de/home/operation/grid-data/current-data.html#wide-area-monitoring
        set_gridtime_offset( atof( argv[1] ) );
    }
    
    init_process_data();
    pthread_t pd_thread_id;
    pthread_create( &pd_thread_id, 0, process_data_thread, (void *)0);
    
    // Create the socket and bind to PORT
    sock = make_socket( PORT );
    if( listen( sock, 1) < 0 )
    {
        perror( "listen" );
        exit( 3 );
    }
    
    slog( "listening on port %d\n", PORT);
    
    int new;
    size = sizeof( struct sockaddr_in );
    
    while( (new = accept( sock, (struct sockaddr *) &clientname, &size)) > 0 )
    {
        slog( "connect from %s, port %u.\n", inet_ntoa( clientname.sin_addr ), ntohs( clientname.sin_port ));
        
        pthread_t thread_id;
        
        if( pthread_create( &thread_id, NULL, connection_handler, (void *)&new) < 0 )
        {
            perror("could not create thread");
            return 1;
        }
    }
     
    if( new < 0 )
    {
        perror( "accept" );
        return 2;
    }
    
    return 0;
}


bool check_connection( message_hdr_t *msg_hdr, int filedes, int *conn_idx);

void *connection_handler( void *arg )
{
    int sock = *(int*)arg;
    
    pthread_detach( pthread_self() );

    slog( "New thread for socket %d\n", sock);
    
    int conn_idx = -1;
    bool done = false;
    while( !done )    
    {
        message_hdr_t msg_hdr;
        
        if( read_from_client_2( sock, (char *)&msg_hdr, sizeof(message_hdr_t)) < 0 )
        {
            fprintf( stderr, "recv'd EOF, closing.\n");
            slog( "recv'd EOF, closing.\n" );
            close( sock );
            done = true;
            remove_connection( sock );
        }
        else
        {
            //printf( "-------------------------------------\nrecv'd message header\n" );
            //printf( "         cmd = " );
            //for( int k=0; k<4; k++)
            //    printf( "%c", msg_hdr.cmd[k]);
            //printf("\n");
                        
            // printf( " data length = %u\n",  msg_hdr.data_length);  // Pico has same endianess

            bool conn_ok = false;
            if( !check_connection( &msg_hdr, sock, &conn_idx) )
            {
                fprintf( stderr, "connection error, closing.\n");
                slog( "connection error, closing.\n" );
                close( sock );
                done = true;
                conn_ok = false;
            }
            else
                conn_ok = true;
            
            if( conn_ok && !command_switch( &msg_hdr, sock, conn_idx) )
            {
                fprintf( stderr, "error, closing.\n");
                slog( "error, closing.\n" );
                close( sock );
                done = true;
                remove_connection( sock );
                conn_idx = -1;
            }
        }
    }
    
    pthread_exit(0);
    
    return 0;
}

void cancel_old_thread( int idx )
{
    assert( idx >=0 && idx < MAX_CONN_SLOT );
    
    conn_slot_t *conn_slot = &( conn_slots[ idx ] );
    
    if( conn_slot->tid != -1 )
    {
        slog( "cancelling old thread\n" );
        pthread_cancel( conn_slot->tid );
    }

    conn_slot->tid = pthread_self();
}


bool check_connection( message_hdr_t *msg_hdr, int filedes, int *conn_idx)
{
    bool error = false;
    const char *cmd = msg_hdr->cmd;
    char idstr[32];
    
    if( strncmp( cmd, "IDNT", 4) == 0 )
    {
        //printf( "Got an ident command\n" );
        slog( "command IDNT\n" );
        const char *t = "IDNT_ACK";

        memset( idstr, 0, 32);
        if( msg_hdr->data_length > 0 && msg_hdr->data_length < 32 )
        {
            uint8_t data[32];
            if( read_data_from_client( filedes, msg_hdr, data) < 0 )
                error = true;

            if( !error )
            {
                proto_ctxt_t pc;
                init_proto_ctxt( &pc, "IDNT");
                
                read_msg( &pc, data, msg_hdr->data_length);
                strncpy( idstr, get_elem_value_s( &pc, 1), 32);
                proto_free_ctxt( &pc );
                
                slog( "this Pico's id: %s\n", idstr);
            }
        }
        else
            error = true;
        
        if( !error )
        {
            message_hdr_reply_t reply;
            memcpy( reply.rply, t, 8);
            reply.data_length = 0;
            
            write_to_client_2( filedes, (const uint8_t *) &reply, sizeof(message_hdr_reply_t));
            
            int cn = search_connection( idstr );
            if( cn < 0 )
            {
                int idx = find_free_connection_slot();

                if( idx >= 0 )
                {
                    add_connection( idx, idstr, filedes);
                    slog( "using new connection slot %d for %s\n", idx, idstr);
                    *conn_idx = idx;
                }
                else
                {
                    slog( "no new connection slot found for %s\n", idstr);
                    error = true;
                }
            }
            else
            {
                int old_filedes = get_filedes( cn );
                if( old_filedes >= 0 )
                {
                    slog( "reusing connection slot %d for %s\n", cn, idstr);
                    
                    if( old_filedes > 0 )
                    {
                        slog( "old filedes %d, new filedes %d\n", old_filedes, filedes);
                        if( old_filedes != filedes )
                        {
                            slog( "closing old filedes %d.\n", old_filedes);
                            close( old_filedes );
                        }
                    }
                    
                    set_filedes( cn, filedes);
                    cancel_old_thread( cn );
                    *conn_idx = cn;
                }
            }

            slog_connections();
        }
    }

    if( error )
        slog( "error finding a connection slot for %s\n", idstr);
    
    return !error;
}


static void sdta_write_data( uint8_t *data );
static void meas_check_data( const sample_t *samples, int size, conn_slot_t *conn_slot);
static void write_incidents( const incident_t *incidents, int size, const char *idstr);

bool command_switch( message_hdr_t *msg_hdr, int filedes, int conn_idx)
{
    const char *cmd = msg_hdr->cmd;
    message_hdr_reply_t reply;
    bool error = false;

    // slog( "Recv'd command %c%c%c%c\n", cmd[0], cmd[1], cmd[2], cmd[3]);
    
    if( strncmp( cmd, "CLSY", 4) == 0 )  // clock sync
    {
        // printf( "Got a clsy command\n" );
        slog( "command CLSY\n" );
        const char *t = "CLSY_ACK";
        memcpy( reply.rply, t, 8);
        reply.data_length = 0;
        struct timespec spec;
        
        clock_gettime(CLOCK_REALTIME, &spec);
        //printf("timespec.tv_sec : %jd\n", (intmax_t)spec.tv_sec);
        //printf("timespec.tv_nsec: %09ld\n", spec.tv_nsec);

        int64_t server_tv_sec = spec.tv_sec;
        uint32_t server_tv_nsec = (uint32_t)spec.tv_nsec;
        proto_ctxt_t pc;
        init_proto_ctxt_ack( &pc, t);
        append_msg_elem_ts( &pc, server_tv_sec);
        append_msg_elem_u32( &pc, server_tv_nsec);
        build_send_buffer( &pc );
        reply.data_length = pc.length;

        assert( sizeof(message_hdr_reply_t) + pc.length <= 32);
        uint8_t msgbuf[32];
        memcpy( msgbuf, (const void *)&reply, sizeof(message_hdr_reply_t));
        memcpy( msgbuf + sizeof(message_hdr_reply_t), (const void *)pc.send_buffer, pc.length);
        proto_free_ctxt( &pc );

        write_to_client_2( filedes, (const uint8_t *)msgbuf, sizeof(message_hdr_reply_t) + pc.length);
    }
    else if( strncmp( cmd, "PING", 4) == 0 )
    {
        // printf( "Got a ping command\n" );
        slog( "command PING\n" );
        const char *t = "PING_ACK";
        memcpy( reply.rply, t, 8);
        reply.data_length = 0;

        write_to_client_2( filedes, (const uint8_t *) &reply, sizeof(message_hdr_reply_t));
    }
    else if( strncmp( cmd, "IDNT", 4) == 0 )
    {
        // already handled in check_connection()
    }
    else if( strncmp( cmd, "MEAS", 4) == 0 )
    {
        // printf( "Got a measurement command\n" );
        // printf( "Payload data length = %u\n", msg_hdr->data_length);
        
        if( msg_hdr->data_length > 0 )
        {
            uint8_t *data = (uint8_t *)malloc( msg_hdr->data_length );
            
            if( read_data_from_client( filedes, msg_hdr, data) < 0 )
                error = true;

            conn_slot_t *conn_slot = 0;
            if( !error )
            {
                if( conn_idx >= 0 )        
                    conn_slot = &(conn_slots[ conn_idx ]);
                else
                {
                    error = true;
                    slog(  "No connection slot for command = MEAS\n" );
                }
            }
            
            if( !error )
            {
                proto_ctxt_t pc;
                init_proto_ctxt( &pc, "MEAS");
                
                read_msg( &pc, data, msg_hdr->data_length);
                int size;
                const sample_t *samples = get_elem_array_samples( &pc, 1, &size);
                
                meas_check_data( samples, size, conn_slot);
                
                send_data( conn_idx, samples, size);  // sends to process_data
                
                proto_free_ctxt( &pc );
            }
            free( data );
        }
        
        const char *t = "MEAS_ACK";
        memcpy( reply.rply, t, 8);
        reply.data_length = 0;
        
        write_to_client_2( filedes, (const uint8_t *) &reply, sizeof(message_hdr_reply_t));
    }
    else if( strncmp( cmd, "INCD", 4) == 0 )
    {
        // printf( "Got an incident command\n" );
        // printf( "Payload data length = %u\n", msg_hdr->data_length);
        
        if( msg_hdr->data_length > 0 )
        {
            uint8_t *data = (uint8_t *)malloc( msg_hdr->data_length );
            
            if( read_data_from_client( filedes, msg_hdr, data) < 0 )
                error = true;
            
            const char *idstr = 0;
            if( !error )
            {
                if( conn_idx >= 0 )        
                    idstr = conn_slots[ conn_idx ].idstr;
                else
                    error = true;
            }
            
            if( !error )
            {
                proto_ctxt_t pc0;
                init_proto_ctxt( &pc0, "INC0");
                
                int p = read_msg( &pc0, data, msg_hdr->data_length);

                uint16_t c_incid = get_elem_value_u16( &pc0, 1);
                
                incident_t *incidents = (incident_t *)malloc( c_incid * sizeof( incident_t ) );

                for( int i = 0; i < c_incid; i++)
                {
                    proto_ctxt_t pc1;
                    init_proto_ctxt( &pc1, "INC1");
                    p += read_msg( &pc1, data + p, msg_hdr->data_length - p);
                    
                    incidents[i].time = get_elem_value_ts( &pc1, 1);
                    const char *reason = get_elem_value_s( &pc1, 2);
                    if( reason )
                        strcpy( incidents[i].reason, reason);
                    
                    int size;
                    const uint32_t *rise_ts = get_elem_array_u32( &pc1, 3, &size);
                    if( size > 0 )
                        memcpy( incidents[i].rise_ts, rise_ts, size * sizeof( uint32_t ));
                    incidents[i].rise_size = size;
                    
                    const uint32_t *fall_ts = get_elem_array_u32( &pc1, 4, &size);
                    if( size > 0 )
                        memcpy( incidents[i].fall_ts, fall_ts, size * sizeof( uint32_t ));
                    incidents[i].fall_size = size;
                    
                    const uint32_t *rise_diffs = get_elem_array_u32( &pc1, 5, &size);
                    if( size > 0 )
                        memcpy( incidents[i].rise_diffs, rise_diffs, size * sizeof( uint32_t ));

                    const uint32_t *fall_diffs = get_elem_array_u32( &pc1, 6, &size);
                    if( size > 0 )
                        memcpy( incidents[i].fall_diffs, fall_diffs, size * sizeof( uint32_t ));
                    
                    proto_free_ctxt( &pc1 );
                }
                
                write_incidents( incidents, c_incid, idstr);

                free( incidents );
                proto_free_ctxt( &pc0 );
            }
            free( data );
        }
        
        const char *t = "INCD_ACK";
        memcpy( reply.rply, t, 8);
        reply.data_length = 0;
        
        write_to_client_2( filedes, (const uint8_t *) &reply, sizeof(message_hdr_reply_t));
    }
    else if( strncmp( cmd, "CLCP", 4) == 0 )  // clock compare
    {
        // printf( "Got a clcp command\n" );
        // slog( "command CLCP\n" );
        const char *t = "CLCP_ACK";
        memcpy( reply.rply, t, 8);
        reply.data_length = 0;
        struct timespec spec;

        uint8_t *data = (uint8_t *)malloc( msg_hdr->data_length );
        
        if( read_data_from_client( filedes, msg_hdr, data) < 0 )
            error = true;
        
        if( !error )
        {
            proto_ctxt_t pc;
            init_proto_ctxt( &pc, "CLCP");
            
            read_msg( &pc, data, msg_hdr->data_length);
            
            int64_t pico_time_us = get_elem_value_ts( &pc, 1);
            proto_free_ctxt( &pc );
            
            char pico_time_str[30];
            time_us_64_to_str( pico_time_str, 30, pico_time_us);
            
            clock_gettime( CLOCK_REALTIME, &spec);
            //printf("tv_sec : %jd\n", (intmax_t)spec.tv_sec);
            //printf("tv_nsec: %09ld\n", spec.tv_nsec);
            
            int64_t server_tv_sec = spec.tv_sec;
            uint32_t server_tv_nsec = (uint32_t)spec.tv_nsec;
            
            // convert timespec to us
            int64_t server_time_us = server_tv_sec * 1000000LL + (int64_t)server_tv_nsec / 1000LL;
            
            char server_time_str[30];
            time_us_64_to_str( server_time_str, 30, server_time_us);
            
            int64_t time_diff = pico_time_us - server_time_us;

            if( time_diff < -10000 || time_diff > 1000 )
            {
                const char *idstr = 0;
                if( conn_idx >= 0 )        
                    idstr = conn_slots[ conn_idx ].idstr;
                
                slog( "%s - clock compare: Pico %s vs server %s, difference %lld us\n", idstr ? idstr : "?", pico_time_str, server_time_str, time_diff);
                
                //if( time_diff > 0 )
                //    slog( "Pico ahead by %lld us\n", time_diff);
            }
            
            proto_ctxt_t pc_ack;
            init_proto_ctxt_ack( &pc_ack, t);
            append_msg_elem_ts( &pc_ack, server_time_us);
            build_send_buffer( &pc_ack );
            reply.data_length = pc_ack.length;
            
            assert( sizeof(message_hdr_reply_t) + pc_ack.length <= 32);
            uint8_t msgbuf[32];
            memcpy( msgbuf, (const void *)&reply, sizeof(message_hdr_reply_t));
            memcpy( msgbuf + sizeof(message_hdr_reply_t), (const void *)pc_ack.send_buffer, pc_ack.length);
            proto_free_ctxt( &pc_ack );
            
            write_to_client_2( filedes, (const uint8_t *)msgbuf, sizeof(message_hdr_reply_t) + pc_ack.length);
        }
        free( data );
    }
    else if( strncmp( cmd, "SDTA", 4) == 0 )  // ADC sample data. This is for a different project. Not mfm.
    {
        printf( "Got a sdta command\n" );
        printf( "Payload data length = %u\n", msg_hdr->data_length);
        
        if( msg_hdr->data_length > 0 )
        {
            uint8_t *data = (uint8_t *)malloc( msg_hdr->data_length );
            
            if( read_data_from_client( filedes, msg_hdr, data) < 0 )
                error = true;
            
            if( !error )
                sdta_write_data( data );
            free( data );
        }

        if( !error )
        {
            const char *t = "SDTA_ACK";
            memcpy( reply.rply, t, 8);
            reply.data_length = 0;
            
            write_to_client_2( filedes, (const uint8_t *) &reply, sizeof(message_hdr_reply_t));
        }
    }

    if( error )
        slog( "command_switch() ends with error\n" );
    return !error;
}


static void sdta_write_data( uint8_t *data )
{
    int i;
    uint16_t *samples = (uint16_t *)data;
    uint32_t *timestamps = (uint32_t *)(data + 20000*sizeof(uint16_t));

    // clean up timestamps
    uint32_t first_ts = timestamps[0];
    int n = 1;
    for( i = 1; i < 20000; i++)
    {
        if( timestamps[i] > timestamps[i-1] )
        {
            n++;
        }
        else
            break;
    }
    
    for( i = 0; i < n; i++)
        timestamps[i] -= first_ts;
    
    static int k = 0;
    char fn[32];
    k++;
    snprintf( fn, 32, "sample_data_%d.txt", k);
    
    FILE *fp = fopen( fn, "w");
    if( !fp )
        return;
    
    printf( "Writing %d samples into %s\n", n, fn);
    for( i = 0; i < n; i++)
    {
        fprintf( fp, "%u  %u\n", timestamps[i], samples[i]);
    }
    
    fclose( fp );
}


static void meas_check_data( const sample_t *samples, int size, conn_slot_t *conn_slot)
{
    int i;
    const char *idstr = conn_slot->idstr;
    uint32_t last_number = conn_slot->last_number;
    uint64_t last_time = conn_slot->last_time;
    
    const int64_t time_min_diff =  500000LL; //(uint64_t) (1/(MAINS_FREQ + 10.) * 50 * 1000000);
    const int64_t time_max_diff = 1300000LL; //(uint64_t) (1/(MAINS_FREQ - 10.) * 50 * 1000000);

    
    for( i = 0; i < size; i++)
    {
        if( last_number == 0 )
            last_number = samples[i].number;
        else
        {
            if( last_number + 1 != samples[i].number )
                slog( "%s - SYSTEM ERROR: measurement %u missing\n", idstr, last_number);
            else if( last_time != 0 )
            {
                int64_t time_diff = samples[i].time - last_time;     // time stamps are in us
                // printf( "%s - time difference of %lld us of adjoined samples\n", idstr, (long long)time_diff);
                
                if( !( time_min_diff < time_diff && time_diff < time_max_diff ) )
                    slog( "%s - SYSTEM ERROR: implausible time difference of %lld us of adjoined samples\n", idstr, (long long)time_diff);
            }
            last_number = samples[i].number;
        }
        
        last_time = samples[i].time;
        
        if( samples[i].clock_state_1pps != CLOCK_STATE_INVALID )
        {
            switch( samples[i].clock_state_1pps )
            {
            case CLOCK_STATE_1PPS_EXTR_ADJ:
                slog( "%s - warning: 1PPS extreme adjustment was needed (measurement %u)\n", idstr, samples[i].number);
                break;
            case CLOCK_STATE_1PPS_NEED_RESY:
                slog( "%s - warning: 1PPS needed resync (measurement %u)\n", idstr, samples[i].number);
                break;
            case CLOCK_STATE_1PPS_BROKEN:
                slog( "%s - warning: 1PPS is broken (measurement %u)\n", idstr, samples[i].number);
                break;
            case CLOCK_STATE_1PPS_WAITING:
                slog( "%s - warning: waiting for proper 1PPS (measurement %u)\n", idstr, samples[i].number);
                break;
            case CLOCK_STATE_1PPS_UNUSU_ADJ:
                slog( "%s - warning: unusual adjustment needed to sync with 1PPS (measurement %u)\n", idstr, samples[i].number);
                break;
            }
        }
        else
            slog( "%s - error: 1PPS invalid state (measurement %u)\n", idstr, samples[i].number);
        
        if( samples[i].clock_state_comp != CLOCK_STATE_INVALID )
        {
            switch( samples[i].clock_state_comp )
            {
            case CLOCK_STATE_COMP_BROKEN:
                slog( "%s - error: network time clock compare difference too high (measurement %u)\n", idstr, samples[i].number);
                break;
            case CLOCK_STATE_COMP_TOO_LONG:
                slog( "%s - warning: network time clock compare took too long (measurement %u)\n", idstr, samples[i].number);
                break;
            }
        }
        else
           slog( "%s - error: network time clock in invalid state (measurement %u)\n", idstr, samples[i].number); 
    }
    
    conn_slot->last_number = last_number;
    conn_slot->last_time = last_time;
}


static void print_atmega_timestamps( int idx, uint64_t time, const uint32_t *meas_ts, const uint32_t *diffs, int cnt_meas)
{
    if( cnt_meas <= 2 )
    {
        file_mgr_fprintf( idx, "%lu ERROR: Not enough timestamps.\n", time);
        return;
    }
    
    file_mgr_fprintf( idx, "%lu %8u\n", time, meas_ts[0]);

    int32_t min_diff = 1000000, max_diff = 0;
    int32_t sum_diff = 0;
    int cnt_ok = 0;
    for( int n = 1; n < cnt_meas; n++)
    {
        int32_t diff = meas_ts[ n ] - meas_ts[ n - 1 ];
        file_mgr_fprintf( idx, "%lu %8u %6d %6u%s\n", time, meas_ts[n], diff, diffs[n], (diffs[n] == 0) ? "  <E" : (diff == diffs[n]) ? "" : "  <C");
        
        if( diffs[n] != 0 )
        {
            cnt_ok++;
            sum_diff += diffs[n];
            
            if( diffs[n] < min_diff )
                min_diff = diffs[n];
            if( diffs[n] > max_diff )
                max_diff = diffs[n];
        }
    }
    
    if( cnt_ok > 0 )
    {
        file_mgr_fprintf( idx, "%lu min/max diff: %d/%d\n", time, min_diff, max_diff);
        double min_vs_max = ((double)min_diff - (double)max_diff) / (double)min_diff;
        file_mgr_fprintf( idx, "%lu min vs max diff: %.4f%%\n", time, min_vs_max);
        
        double avg_diff = (double)sum_diff / (double)cnt_ok;
        file_mgr_fprintf( idx, "%lu avg diff: %f\n", time, avg_diff);
        double sum_std_dev = 0.;
        for( int n = 1; n < cnt_meas; n++)
            if( diffs[n] > 0 )
                sum_std_dev += ((double)diffs[n] - avg_diff) * ((double)diffs[n] - avg_diff);
        double std_dev = sqrt( sum_std_dev / (double)cnt_ok );
        file_mgr_fprintf( idx, "%lu diff std dev: %.6f\n", time, std_dev);
        double freq = (double)ATMEGA_FREQ/avg_diff;
        file_mgr_fprintf( idx, "%lu freq: %.5f\n", time, freq);
    }
}


static void write_incidents( const incident_t *incidents, int size, const char *idstr)
{
    int i;
    char fn[50];
    snprintf( fn, 50, "incidents_%s", idstr);

    int file_id = reg_file( fn );
    if( file_id < 0 )
        return;
    
    slog( "%s - %d incident%s\n", idstr, size, size > 1 ? "s" : "");

    int idx;
    for( i = 0; i < size; i++)
    {
        idx = rotate_file( incidents[i].time, file_id);
        if( idx < 0 )
            return;
        
        char buf[30];
        time_us_64_to_str( buf, 30, incidents[i].time);
        file_mgr_fprintf( idx, "%lu %s  %s\n", incidents[i].time, buf, incidents[i].reason);
        
        if( incidents[i].rise_size > 0 )
        {
            file_mgr_fprintf( idx, "%lu rise:\n", incidents[i].time);
            print_atmega_timestamps( idx, incidents[i].time, incidents[i].rise_ts, incidents[i].rise_diffs, incidents[i].rise_size);
        }
        
        if( incidents[i].fall_size > 0 )
        {
            file_mgr_fprintf( idx, "%lu fall:\n", incidents[i].time);
            print_atmega_timestamps( idx, incidents[i].time, incidents[i].fall_ts, incidents[i].fall_diffs, incidents[i].fall_size);
        }
        
        file_mgr_fprintf( idx, "%lu INCID_END\n", incidents[i].time);
    }

    if( size > 0 )
        file_mgr_fflush( idx );
}
