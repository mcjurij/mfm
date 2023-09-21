#ifndef _TCP_CLIENT_H_
#define _TCP_CLIENT_H_

#ifdef TCP_SERVER_IP
#include "lwip/tcp.h"

#include "freq.h"
#include "ringbuffer.h"

#define RECV_BUF_SIZE 128

typedef struct
{
    char     rply[8];
    uint16_t data_length;
} message_hdr_reply_t;

typedef struct TCP_CLIENT_T_ {
    struct tcp_pcb *tcp_pcb;
    ip_addr_t remote_addr;
    uint8_t recv_buffer[RECV_BUF_SIZE];
    int recv_buffer_len;
    int sent_len;
    message_hdr_reply_t reply;
    bool complete;
    bool connected;
    bool error;

    uint32_t total_bytes_sent, last_total_bytes_sent;
} TCP_CLIENT_T;

TCP_CLIENT_T *client_open();
void client_close( TCP_CLIENT_T *state );
void client_abort( TCP_CLIENT_T *state );

bool client_do_ping( TCP_CLIENT_T *state );
bool client_do_idnt( TCP_CLIENT_T *state, const char *pico_idstr);

uint32_t client_do_clsy( TCP_CLIENT_T *state, struct timespec *spec);    // clock sync
void client_do_clcp( TCP_CLIENT_T *state, int64_t pico_time_us, int64_t *server_time_us);  // clock compare

bool client_do_meas( TCP_CLIENT_T *state, ringbuffer_t *samples, int c_to_send);
bool client_do_incd( TCP_CLIENT_T *state, ringbuffer_t *incidents, int c_to_send);

void client_try_reconnect( TCP_CLIENT_T *state, const char *pico_idstr);

#endif

#endif
