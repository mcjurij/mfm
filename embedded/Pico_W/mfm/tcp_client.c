#ifdef TCP_SERVER_IP
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"

#include "tcp_client.h"
#include "ntime.h"
#include "proto.h"

#define TCP_PORT 4200

#define POLL_TIME_S 5


// #define DEBUG_TRAFFIC

#ifdef DEBUG_TRAFFIC
#define DEBUG_printf printf
#else
#define DEBUG_printf(FS,...) 
#endif

#ifdef DEBUG_TRAFFIC
static void dump_bytes( const uint8_t *bptr, uint32_t len)
{
    unsigned int i = 0;

    printf("dump_bytes %d", len);
    for (i = 0; i < len; i++)
    {
        if ((i & 0x0f) == 0)
            printf("\n");
        else if ((i & 0x07) == 0) 
            printf(" ");

        if( isprint( bptr[i] ) )
            printf("%c ", bptr[i]);
        else
            printf("%02x ", bptr[i]);
    }
    printf("\n");
}
#define DUMP_BYTES dump_bytes
#else
#define DUMP_BYTES(A,B)
#endif

#ifdef DEBUG_TRAFFIC
void dump_data( const uint8_t *buffer, int len, bool print_readable)
{
    int k = 0;
    for( int i = 0; i < len; i++)
    {
        const char c = buffer[i];
        if( print_readable && isprint(c) )
            printf( " %c", c);
        else
            printf( " %02x", c);
        if( ++k == 16 )
        {
            printf( "\n" );
            k = 0;
        }
    }
    printf( "\n" );
}
#endif


typedef struct
{
    char     cmd[4];
    uint32_t data_length;
} message_hdr_t;


static err_t tcp_client_close( void *arg )
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    err_t err = ERR_OK;
    
    state->connected = false;
    
    if( state->tcp_pcb != NULL )
    {
        tcp_arg(state->tcp_pcb, NULL);
        tcp_poll(state->tcp_pcb, NULL, 0);
        tcp_sent(state->tcp_pcb, NULL);
        tcp_recv(state->tcp_pcb, NULL);
        tcp_err(state->tcp_pcb, NULL);
        
        err = tcp_close(state->tcp_pcb);
        if (err != ERR_OK)
        {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(state->tcp_pcb);
            state->error = true;
            return ERR_ABRT;
        }
        state->tcp_pcb = NULL;
    }
    
    return err;
}


// Called with results of operation
static err_t tcp_result( void *arg, int status)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    
    state->complete = true;
    if( status < 0 )
        state->error = true;
    return tcp_client_close(arg);
}


static err_t tcp_client_sent( void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    
    DEBUG_printf("tcp_client_sent len = %u\n", len);
    state->sent_len += len;
    DEBUG_printf("tcp_client_sent sent len = %u\n", state->sent_len);
    
    return ERR_OK;
}


static err_t tcp_client_connected( void *arg, struct tcp_pcb *tpcb, err_t err)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    
    if( err != ERR_OK )
    {
        printf("connect failed %d\n", err);
        return tcp_result( arg, err);
    }
    
    state->connected = true;
    state->complete = false;
    state->error = false;
    
    return ERR_OK;
}


static err_t tcp_client_poll( void *arg, struct tcp_pcb *tpcb)
{
    DEBUG_printf("tcp_client_poll\n");
    printf( "tcp_client_poll(): @%.5fs\n", ((double)time_us_64()) / 1000000.);
    // return tcp_result( arg, -1); // no response is an error?
    
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    err_t err = ERR_OK;
    
    static int cnt_nb = 0;
    if( state->total_bytes_sent == state->last_total_bytes_sent )
    {
        printf( "tcp_client_poll(): no bytes sent\n");
        cnt_nb++;

        if( cnt_nb > 12 /* 20 */ )
        {
            printf( "tcp_client_poll(): ... for too long\n");
            cnt_nb = 0;
            
            tcp_arg(state->tcp_pcb, NULL);
            tcp_poll(state->tcp_pcb, NULL, 0);
            tcp_sent(state->tcp_pcb, NULL);
            tcp_recv(state->tcp_pcb, NULL);
            tcp_err(state->tcp_pcb, NULL);
            
            tcp_abort( state->tcp_pcb );
            state->tcp_pcb = NULL;
            err = ERR_ABRT;
            state->error = true;
            state->connected = false;
        }
    }
    else
    {
        cnt_nb = 0;
        
        state->last_total_bytes_sent = state->total_bytes_sent;
    }
    return err;
}


static void tcp_client_err( void *arg, err_t err)
{
    if (err != ERR_ABRT)
    {
        DEBUG_printf("tcp_client_err %d\n", err);
        TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
        state->error = true;
        tcp_result( arg, err);
    }
}


static err_t tcp_client_recv( void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    
    DEBUG_printf("tcp_client_recv(): tcp_client_recv called!\n");
    
    if( !p )
    {
        DEBUG_printf("tcp_client_recv(): p is NULL\n");
        return tcp_result(arg, -1);
    }
    
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    if( p->tot_len > 0 )
    {
        DEBUG_printf("tcp_client_recv(): recv %d err %d\n", p->tot_len, err);

        for (struct pbuf *q = p; q != NULL; q = q->next)
        {
            DUMP_BYTES(q->payload, q->len);
        }
        
        // Receive the buffer
        const uint16_t buffer_left = RECV_BUF_SIZE - state->recv_buffer_len;
        DEBUG_printf("tcp_client_recv(): recv %d left\n", buffer_left);
        state->recv_buffer_len += pbuf_copy_partial( p, state->recv_buffer + state->recv_buffer_len,
                                                     p->tot_len > buffer_left ? buffer_left : p->tot_len, 0);

#ifdef DEBUG_TRAFFIC
        printf( "Recv buffer now:\n" );
        dump_data( state->recv_buffer, state->recv_buffer_len, false);
#endif
        
        DEBUG_printf("tcp_client_recv(): tot_len = %u\n", p->tot_len);
        tcp_recved( tpcb, p->tot_len);
    }
    pbuf_free(p);
    DEBUG_printf("tcp_client_recv(): recv buffer len = %d\n", state->recv_buffer_len);

    return ERR_OK;
}

static bool tcp_client_open( void *arg )
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    
    DEBUG_printf("Connecting to %s port %u\n", ip4addr_ntoa(&state->remote_addr), TCP_PORT);
    
    state->tcp_pcb = tcp_new_ip_type(IP_GET_TYPE(&state->remote_addr));
    if (!state->tcp_pcb)
    {
        printf("failed to create pcb\n");
        return false;
    }

    tcp_arg(state->tcp_pcb, state);
    tcp_poll(state->tcp_pcb, tcp_client_poll, POLL_TIME_S * 2);
    tcp_sent(state->tcp_pcb, tcp_client_sent);
    tcp_recv(state->tcp_pcb, tcp_client_recv);
    tcp_err(state->tcp_pcb, tcp_client_err);

    state->recv_buffer_len = 0;

    // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
    // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
    // these calls are a no-op and can be omitted, but it is a good practice to use them in
    // case you switch the cyw43_arch type later.
    cyw43_arch_lwip_begin();
    err_t err = tcp_connect( state->tcp_pcb, &state->remote_addr, TCP_PORT, tcp_client_connected);
    cyw43_arch_lwip_end();

    return err == ERR_OK;
}


// Perform initialisation
static TCP_CLIENT_T* tcp_client_init()
{
    TCP_CLIENT_T *state = calloc(1, sizeof(TCP_CLIENT_T));
    
    if( !state )
    {
        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }
    
    ip4addr_aton( TCP_SERVER_IP, &state->remote_addr);
    
    state->total_bytes_sent = state->last_total_bytes_sent = 0;
    
    return state;
}


static void read_bytes( TCP_CLIENT_T *state, int bytes_to_read)
{
    int c = 0;
    while( !state->error && state->recv_buffer_len < bytes_to_read)
    {
        if( c%100 == 0 )
            printf("read_bytes(): Waiting for reply from server...\n");
        c++;
        // the following #ifdef is only here so this same example can be used in multiple modes;
        // you do not need it in your code
#if PICO_CYW43_ARCH_POLL
        // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
        // main loop (not from a timer) to check for WiFi driver or lwIP work that needs to be done.
        cyw43_arch_poll();
        busy_wait_ms(1);
#else
        // if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
        // is done via interrupt in the background. This sleep is just an example of some (blocking)
        // work you might be doing.
        busy_wait_ms(1);
#endif
    }
}


static int write_bytes( TCP_CLIENT_T *state, const void *data, int bytes_to_write, int expect_sent_len)
{
    cyw43_arch_lwip_begin();
    err_t err = tcp_write( state->tcp_pcb, data, (u16_t)bytes_to_write, TCP_WRITE_FLAG_COPY);
    cyw43_arch_lwip_end();
    if( err != ERR_OK )
    {
        printf("write_bytes(): failed to enqueue data %d\n", err);
        tcp_result( state, -1);
        return -1;
    }
    
    cyw43_arch_lwip_begin();
    err = tcp_output( state->tcp_pcb );
    cyw43_arch_lwip_end();
    if (err != ERR_OK) {
        printf("write_bytes(): failed to write data %d\n", err);
        tcp_result( state, -1);
        return -1;
    }
    
    DEBUG_printf( "write_bytes(): bytes_to_write = %d, expect_sent_len = %d\n", bytes_to_write, expect_sent_len);
    
    int c = 0, k = 0;
    while( state->sent_len < expect_sent_len && !state->error )
    {
        if( c > 10 )
            if( c%100 == 0 )
            {
                k++;
                printf("write_bytes(): waiting to send... %d\n", k);
                
                if( k > 250 )
                {
                     printf("write_bytes(): ... for too long\n");
                     tcp_result( state, -1);
                }
            }
        c++;
        
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_poll();
        // sleep_ms(1);
        busy_wait_ms(1);
#else
        busy_wait_ms(3);
#endif
    }

    DEBUG_printf( "write_bytes(): state->sent_len = %d\n", state->sent_len);
    state->total_bytes_sent += state->sent_len;
    return bytes_to_write;
}


static int write_data( TCP_CLIENT_T *state, const void *data, int len, int *expect_sent_len)
{
    int chunk_size = 1452;
    int bytes_written = 0;
    
    while( bytes_written < len && !state->error)
    {
        int bytes_to_write = ( len - bytes_written > chunk_size ) ? chunk_size : len - bytes_written;
        
        DEBUG_printf("write_data(): writing %d bytes...\n", bytes_to_write);
        *expect_sent_len += bytes_to_write;

        int w = write_bytes( state, data + bytes_written, bytes_to_write, *expect_sent_len);
        if( w > 0 )
        {
            bytes_written += w;
            DEBUG_printf("write_data(): bytes_written = %d from %d\n", bytes_written, len);
        }
        else
            return -1;
    }

    return bytes_written;
}


static int write_cmd( const char *cmd, TCP_CLIENT_T *state, const void *send_data, int send_len)
{
    message_hdr_t msg;
    
    memcpy( msg.cmd, cmd, 4);
    msg.data_length = send_len; // Intel has same endianess (little)
    
    DEBUG_printf("write_cmd(): Total payload bytes to write: %u\n", msg.data_length);

    int expect_sent_len = sizeof(message_hdr_t);
    cyw43_arch_lwip_begin();
    state->sent_len = 0;
    err_t err = tcp_write( state->tcp_pcb, &msg, sizeof(message_hdr_t), TCP_WRITE_FLAG_COPY);
    cyw43_arch_lwip_end();
    if( err != ERR_OK )
    {
        printf("write_cmd(): failed to enqueue data %d\n", err);
        tcp_result( state, -1);
        return -1;
    }


    if( send_len == 0 )
    {
        cyw43_arch_lwip_begin();
        err = tcp_output( state->tcp_pcb );
        cyw43_arch_lwip_end();
        if (err != ERR_OK) {
            printf("write_cmd(): failed to write data %d\n", err);
            tcp_result( state, -1);
            return -1;
        }
        
        int c = 0, k = 0;
        while( !state->error && state->sent_len < expect_sent_len )
        {
            if( c > 10 )
                if( c%100 == 0 )
                {
                    k++;
                    printf("write_cmd(): Waiting to send hdr %d...\n", k);
                    
                    if( k > 40 )
                    {
                        printf("write_cmd(): ... for too long\n");
                        tcp_result( state, -1);
                    }
                }
            c++;
            
#if PICO_CYW43_ARCH_POLL
            cyw43_arch_poll();
            busy_wait_ms(1);
#else
            busy_wait_ms(5);
            // sleep_ms( 5 ); //does not work? why?
#endif
        }
    }
    else
        return write_data( state, send_data, send_len, &expect_sent_len);
    
    return 0;
}


TCP_CLIENT_T *client_open()
{
       
#if PICO_CYW43_ARCH_POLL
    DEBUG_printf("client_open(): Using poll mode\n");
#else
    DEBUG_printf("client_open(): Using interrupt mode\n");
#endif
    
    TCP_CLIENT_T *state = tcp_client_init();
    if( !state )
        return 0;
    
    if( !tcp_client_open( state ) )
    {
        printf("client_open(): Could not open TCP connection.\n");
        
        tcp_result( state, -1);
        free( state );
        return 0;
    }
    else
        DEBUG_printf("client_open(): TCP connection established.\n");

    return state;
}


void client_close(TCP_CLIENT_T *state)
{
    cyw43_arch_lwip_begin();
    tcp_arg(state->tcp_pcb, NULL);
    tcp_poll(state->tcp_pcb, NULL, 0);
    tcp_sent(state->tcp_pcb, NULL);
    tcp_recv(state->tcp_pcb, NULL);
    tcp_err(state->tcp_pcb, NULL);
    
    err_t err = tcp_close(state->tcp_pcb);
    if (err != ERR_OK)
    {
        DEBUG_printf("client_close: close failed %d, calling abort\n", err);
        tcp_abort( state->tcp_pcb );
        state->error = true;
    }
    cyw43_arch_lwip_end();
    
    state->tcp_pcb = NULL;
    state->connected = false;
}


void client_abort( TCP_CLIENT_T *state )
{
    if( state->tcp_pcb )
    {
        cyw43_arch_lwip_begin();
        tcp_arg(state->tcp_pcb, NULL);
        tcp_poll(state->tcp_pcb, NULL, 0);
        tcp_sent(state->tcp_pcb, NULL);
        tcp_recv(state->tcp_pcb, NULL);
        tcp_err(state->tcp_pcb, NULL);
        
        tcp_abort( state->tcp_pcb );
        cyw43_arch_lwip_end();
        state->tcp_pcb = NULL;
    }
    
    state->error = true;
    state->connected = false;
}


static bool client_do_conversation( TCP_CLIENT_T *state, const char *cmd, const void *send_data, int send_len, void *reply_data, int *reply_len)
{
    if( !state )
    {
        printf( "client_do_conversation(): state is NULL\n" );
        return false;
    }
    
    const char *t = 0;
    const char *expect_rply = 0;
    
    if( strncmp( cmd, "CLSY", 4) == 0 )
    {
        t = "CLSY";
        expect_rply = "CLSY_ACK";
    }
    else if( strncmp( cmd, "PING", 4) == 0 )
    {
        t = "PING";
        expect_rply = "PING_ACK";
    }
    else if( strncmp( cmd, "IDNT", 4) == 0 )
    {
        t = "IDNT";
        expect_rply = "IDNT_ACK";
    }
    else if( strncmp( cmd, "MEAS", 4) == 0 )
    {
        t = "MEAS";
        expect_rply = "MEAS_ACK";
    }
    else if( strncmp( cmd, "INCD", 4) == 0 )
    {
        t = "INCD";
        expect_rply = "INCD_ACK";
    }
    else if( strncmp( cmd, "CLCP", 4) == 0 )
    {
        t = "CLCP";
        expect_rply = "CLCP_ACK";
    }
    else if( strncmp( cmd, "SDTA", 4) == 0 )
    {
        t = "SDTA";
        expect_rply = "SDTA_ACK";
    }

    if( t == 0 )
    {
        printf("client_do_conversation(): Unknown command '%s'.\n", cmd);
        return false;
    }
    memset( state->reply.rply, 0, 8);
    
    DEBUG_printf("client_do_conversation(): Entering conversation...\n");
    
    state->recv_buffer_len = 0;
    if( write_cmd( t, state, send_data, send_len) < 0 )
    {
        printf("client_do_conversation(): write_cmd() failed.\n");
        state->error = true;
        return false;
    }
    
    int c = 0;
    while( !state->error && state->recv_buffer_len < sizeof( message_hdr_reply_t ))
    {
        if( c%100 == 0 )
            printf("Waiting for reply from server...\n");
        c++;
        // the following #ifdef is only here so this same example can be used in multiple modes;
        // you do not need it in your code
#if PICO_CYW43_ARCH_POLL
        // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
        // main loop (not from a timer) to check for WiFi driver or lwIP work that needs to be done.
        cyw43_arch_poll();
        busy_wait_ms(1);
#else
        // if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
        // is done via interrupt in the background. This sleep is just an example of some (blocking)
        // work you might be doing.
        busy_wait_ms(5);
#endif
    }
    
    if( !state->error && state->recv_buffer_len >= sizeof( message_hdr_reply_t ) )
        memcpy( &(state->reply), state->recv_buffer, sizeof( message_hdr_reply_t ));
    
    
    if( state->complete )
        DEBUG_printf("client_do_conversation(): conversation complete.\n");

    if( !state->error )
    {
        if( memcmp( state->reply.rply, expect_rply, 8) == 0 )
        {
            DEBUG_printf("client_do_conversation(): conversation ack'd as expected.\n");
            // DEBUG_printf("client_do_conversation(): ack data len = %u.\n", state->reply.data_length);

            if( state->reply.data_length > 0 )
            {
                int bytes_to_read = sizeof( message_hdr_reply_t ) + state->reply.data_length;

                read_bytes( state, bytes_to_read);

                if( !state->error && reply_data && *reply_len >= state->reply.data_length )
                {
                    *reply_len = state->reply.data_length;
                    memcpy( reply_data, state->recv_buffer + sizeof( message_hdr_reply_t ), state->reply.data_length);
                    DEBUG_printf("client_do_conversation(): copied %u bytes of reply data.\n", state->reply.data_length);
                }
                else
                    DEBUG_printf("client_do_conversation(): Not copied bytes to reply data. Buffer too small. Or one or more errors.\n" );
            }
            
        }
        else
        {
            DEBUG_printf("client_do_conversation(): conversation failed, wrong ack.\n");
            state->error = true;
        }
    }
    else
        DEBUG_printf("client_do_conversation(): conversation failed. Has one or more errors.\n");

    return !state->error;
}


bool client_do_ping( TCP_CLIENT_T *state )
{
    int reply_len = 0;
    return client_do_conversation( state, "PING", 0, 0, 0, &reply_len);
}


bool client_do_idnt( TCP_CLIENT_T *state, const char *pico_idstr)
{
    int reply_len = 0;
    proto_ctxt_t pc;
    const char *s = "IDNT";
    init_proto_ctxt( &pc, s);
    append_msg_elem_s( &pc, pico_idstr);
    build_send_buffer( &pc );
    bool ok = client_do_conversation( state, s, pc.send_buffer, pc.length, 0, &reply_len);
    proto_free_ctxt( &pc );
    return ok;
}


uint32_t client_do_clsy( TCP_CLIENT_T *state, struct timespec *spec)   // clock sync
{
    uint8_t rply_buf[16];
    int reply_len = 16;
    uint32_t s = time_us_32();
    client_do_conversation( state, "CLSY", 0, 0, rply_buf, &reply_len);
    uint32_t rtt = time_us_32() - s;
    printf( "RTT %uus\n", rtt);
    
#ifdef DEBUG_TRAFFIC
    printf( "Reply:\n" );
    dump_data( rply_buf, reply_len, false);
#endif

    proto_ctxt_t pc;
    init_proto_ctxt_ack( &pc, "CLSY_ACK");

    read_msg( &pc, rply_buf, reply_len);
    spec->tv_sec = get_elem_value_ts( &pc, 1);  // time_t is 64 bit
    spec->tv_nsec = get_elem_value_u32( &pc, 2);
    proto_free_ctxt( &pc );
    
    char buf[30];
    timespec2str( buf, 30, spec, 1);
    printf( "Time on server: %s\n", buf);

    return rtt;
}


void client_do_clcp( TCP_CLIENT_T *state, int64_t pico_time_us, int64_t *server_time_us)   // clock compare
{
    uint8_t rply_buf[16];
    int reply_len = 16;
    proto_ctxt_t pc, pc_ack;
    
    *server_time_us = 0;
    
    init_proto_ctxt( &pc, "CLCP");
    append_msg_elem_ts( &pc, pico_time_us);
    build_send_buffer( &pc );

    bool ok = client_do_conversation( state, "CLCP", pc.send_buffer, pc.length, rply_buf, &reply_len);
    proto_free_ctxt( &pc );
    
    if( !ok )
        return;
    
#ifdef DEBUG_TRAFFIC
    printf( "Reply:\n" );
    dump_data( rply_buf, reply_len, false);
#endif

    init_proto_ctxt_ack( &pc_ack, "CLCP_ACK");
    read_msg( &pc_ack, rply_buf, reply_len);
    *server_time_us = get_elem_value_ts( &pc_ack, 1);
    proto_free_ctxt( &pc_ack );
    
    //char buf[30];
    //time_us_64_to_str( buf, 30, *server_time_us);
    //printf( "Time on server: %s\n", buf);
}


bool client_do_meas( TCP_CLIENT_T *state, ringbuffer_t *samples, int c_to_send)  // send measurements
{
    int reply_len = 0;
    proto_ctxt_t pc;
    const char *s = "MEAS";

    init_proto_ctxt( &pc, s);
    append_msg_elem_array_samples_from_ringbuffer( &pc, samples, c_to_send);
    build_send_buffer( &pc );
    
    if( !pc.send_buffer )
    {
        proto_free_ctxt( &pc );
        return true;  // don't try to resend
    }
    
    bool ok = client_do_conversation( state, s, pc.send_buffer, pc.length, 0, &reply_len);

    proto_free_ctxt( &pc );
    /*    printf( "Reply:\n" );
    dump_data( rply_buf, sizeof(int64_t) + sizeof(uint32_t), false);

    memcpy( (void *)spec, rply_buf, sizeof(int64_t) + sizeof(uint32_t));
    */

    if( !ok )
        printf( "client_do_meas(): result is not ok\n" );
    return ok;
}


bool client_do_incd( TCP_CLIENT_T *state, ringbuffer_t *incidents, int c_to_send)
{
    int reply_len = 0;
    proto_ctxt_t pc0, pc1;
    const char *s = "INCD";
    
    init_proto_ctxt( &pc0, "INC0");  // incident container
    append_msg_elem_u16( &pc0, (uint16_t)c_to_send);
    build_send_buffer( &pc0 );

    if( !pc0.send_buffer )
    {
        proto_free_ctxt( &pc0 );
        return true;  // don't try to resend
    }
    
    for( int i = 0; i < c_to_send; i++)
    {
        incident_t *incident = consumer_get_next( incidents, c_to_send);
        
        init_proto_ctxt( &pc1, "INC1");  // single incident
        append_msg_elem_ts( &pc1, incident->time);
        append_msg_elem_s( &pc1, incident->reason);
        append_msg_elem_array_u32( &pc1, incident->rise_ts, incident->rise_size);
        append_msg_elem_array_u32( &pc1, incident->fall_ts, incident->fall_size);
        append_msg_elem_array_u32( &pc1, incident->rise_diffs, incident->rise_size);
        append_msg_elem_array_u32( &pc1, incident->fall_diffs, incident->fall_size);
        
        build_send_buffer( &pc1 );
        if( !pc1.send_buffer )
        {
            proto_free_ctxt( &pc1 );
            proto_free_ctxt( &pc0 );
            return true;  // don't try to resend
        }
        
        append_send_buffer( &pc0, &pc1); // append to container
        
        proto_free_ctxt( &pc1 );
    }

    // dump_data( pc0.send_buffer, pc0.length, true);
    
    bool ok = client_do_conversation( state, s, pc0.send_buffer, pc0.length, 0, &reply_len);
    
    proto_free_ctxt( &pc0 );
    
    if( !ok )
        printf( "client_do_incd(): result is not ok\n" );
    return ok;
}


void client_try_reconnect( TCP_CLIENT_T *state, const char *pico_idstr)
{
    if( state )
    {
        if( state->connected )
            client_abort( state );
        free( state );
    }
    
    bool done = false;
    int kr = 0, ko = 0;
    while( !done )
    {
        busy_wait_ms( 250 + rand() % 400 );
        
        state = client_open();
        
        if( state )
        {
            printf( "trying to reconnect... %d\n", ++kr);
            if( client_do_idnt( state, pico_idstr) )
            {
                done = true;
                printf( "sending id string - ok\n" );
            }
            else
            {
                if( state->tcp_pcb )
                    client_abort( state );
                free( state );
                state = 0;
                busy_wait_ms( 250 );
            }
        }
        else
        {
            // once we come here, we're pretty much doomed
            printf( "open failed -- waiting... %d\n", ++ko);
            busy_wait_ms( 1200 + rand() % 500 );
        }
    }
}

#endif
