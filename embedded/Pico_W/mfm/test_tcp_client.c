#ifdef TEST_TCP_SERVER_IP

#include <string.h>
#include <time.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/init.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "test_tcp_client.h"


#define TEST_TCP_PORT 4242
#define BUF_SIZE 2048

#define TEST_ITERATIONS 10
#define POLL_TIME_S 5


#define DEBUG_TRAFFIC
#define DEBUG_DUMP_BYTES

#ifdef DEBUG_TRAFFIC
#define DEBUG_printf printf
#else
#define DEBUG_printf(FS,...) 
#endif

#ifdef DEBUG_TRAFFIC
#ifdef DEBUG_DUMP_BYTES
static void dump_bytes( const uint8_t *bptr, uint32_t len)
{
    int i = 0;

    printf("dump_bytes %d ------------------\n", len);
    int c = 0;
    
    for (i = 0; i < len; i++)
    {
        if ((i & 0x0f) == 0)
            printf("\n%3d: ", i);
        else if ((i & 0x07) == 0)
            printf(" ");

        if( isprint( bptr[i] ) )
        {
            if( !isspace(bptr[i]) )
            {
                printf("%c  ", bptr[i]);
                c++;
            }
            else
                printf("%02x ", bptr[i]);
        }
        else
        {
            printf("%02x ", bptr[i]);
            c++;
        }
    }
    printf("\n--------------------------------\n");
}
#define DUMP_BYTES dump_bytes
#else
#define DUMP_BYTES(A,B)
#endif
#endif


typedef struct TCP_CLIENT_T_ {
    struct tcp_pcb *tcp_pcb;
    ip_addr_t remote_addr;
    uint8_t buffer[BUF_SIZE];
    int buffer_len;
    int sent_len;
    bool complete;
    int run_count;
    bool connected;
} TCP_CLIENT_T;

static err_t tcp_client_close(void *arg) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    err_t err = ERR_OK;
    if (state->tcp_pcb != NULL) {
        tcp_arg(state->tcp_pcb, NULL);
        tcp_poll(state->tcp_pcb, NULL, 0);
        tcp_sent(state->tcp_pcb, NULL);
        tcp_recv(state->tcp_pcb, NULL);
        tcp_err(state->tcp_pcb, NULL);
        err = tcp_close(state->tcp_pcb);
        if (err != ERR_OK) {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(state->tcp_pcb);
            err = ERR_ABRT;
        }
        state->tcp_pcb = NULL;
    }
    return err;
}

// Called with results of operation
static err_t tcp_result(void *arg, int status) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (status == 0) {
        printf("tcp_result: test success\n");
    } else {
        printf("tcp_result: test failed %d\n", status);
    }
    state->complete = true;
    return tcp_client_close(arg);
}

static err_t tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    DEBUG_printf("tcp_client_sent(): tcp_client_sent %u\n", len);
    state->sent_len += len;
    DEBUG_printf("tcp_client_sent():  state->sent_len = %u\n",  state->sent_len);
    
    if (state->sent_len >= BUF_SIZE) {

        state->run_count++;
        if (state->run_count >= TEST_ITERATIONS) {
            tcp_result(arg, 0);
            return ERR_OK;
        }

        // We should receive a new buffer from the server
        state->buffer_len = 0;
        state->sent_len = 0;
        DEBUG_printf("tcp_client_sent(): Waiting for buffer from server (sent)\n");
    }

    return ERR_OK;
}

static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (err != ERR_OK) {
        printf("connect failed %d\n", err);
        return tcp_result(arg, err);
    }
    state->connected = true;
    DEBUG_printf("Waiting for buffer from server (connected)\n");
    return ERR_OK;
}

static err_t tcp_client_poll(void *arg, struct tcp_pcb *tpcb) {
    DEBUG_printf("tcp_client_poll\n");
    return tcp_result(arg, -1); // no response is an error?
}

static void tcp_client_err(void *arg, err_t err) {
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err %d\n", err);
        tcp_result(arg, err);
    }
}

static err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    DEBUG_printf( "tcp_client_recv(): tcp_client_recv called!\n");
    
    if (!p) {
        DEBUG_printf("p is NULL\n");
        return tcp_result(arg, -1);
    }
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    if (p->tot_len > 0) {
        DEBUG_printf( "tcp_client_recv(): recv %d err %d\n", p->tot_len, err);


        for (struct pbuf *q = p; q != NULL; q = q->next) {
            DUMP_BYTES(q->payload, q->len);
        }
        
        // Receive the buffer
        const uint16_t buffer_left = BUF_SIZE - state->buffer_len;
        DEBUG_printf( "tcp_client_recv(): recv %d left\n", buffer_left);
        state->buffer_len += pbuf_copy_partial(p, state->buffer + state->buffer_len,
                                               p->tot_len > buffer_left ? buffer_left : p->tot_len, 0);
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);

    DEBUG_printf("tcp_client_recv(): recv buffer_len = %d\n", state->buffer_len);
    // If we have received the whole buffer, send it back to the server
    if (state->buffer_len == BUF_SIZE)
    {
        DEBUG_printf("tcp_client_recv(): ---- to server ------------------------\n");
        DEBUG_printf("tcp_client_recv(): Writing %d bytes to server\n", state->buffer_len);
        DUMP_BYTES( state->buffer, state->buffer_len);
        DEBUG_printf("tcp_client_recv(): ---- end of to server -----------------\n");
        
        err_t err = tcp_write(tpcb, state->buffer, state->buffer_len, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) {
            DEBUG_printf("Failed to enqueue data %d\n", err);
            return tcp_result(arg, -1);
        }

        err = tcp_output( tpcb );        
        if (err != ERR_OK) {
            DEBUG_printf("Failed to write data %d\n", err);
            return tcp_result(arg, -1);
        }
    }
    
    return ERR_OK;
}


static bool tcp_client_open(void *arg)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    DEBUG_printf("Connecting to %s port %u\n", ip4addr_ntoa(&state->remote_addr), TEST_TCP_PORT);
    state->tcp_pcb = tcp_new_ip_type(IP_GET_TYPE(&state->remote_addr));
    if (!state->tcp_pcb) {
        DEBUG_printf("failed to create pcb\n");
        return false;
    }

    tcp_arg(state->tcp_pcb, state);
    tcp_poll(state->tcp_pcb, tcp_client_poll, POLL_TIME_S * 2);
    tcp_sent(state->tcp_pcb, tcp_client_sent);
    tcp_recv(state->tcp_pcb, tcp_client_recv);
    tcp_err(state->tcp_pcb, tcp_client_err);

    state->buffer_len = 0;

    // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
    // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
    // these calls are a no-op and can be omitted, but it is a good practice to use them in
    // case you switch the cyw43_arch type later.
    cyw43_arch_lwip_begin();
    err_t err = tcp_connect( state->tcp_pcb, &state->remote_addr, TEST_TCP_PORT, tcp_client_connected);
    cyw43_arch_lwip_end();

    return err == ERR_OK;
}

// Perform initialisation
static TCP_CLIENT_T* tcp_client_init(void) {
    TCP_CLIENT_T *state = calloc(1, sizeof(TCP_CLIENT_T));
    if (!state) {
        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }
    ip4addr_aton(TEST_TCP_SERVER_IP, &state->remote_addr);
    return state;
}

void run_tcp_client_test()
{
    printf( "lwip version %s\n", LWIP_VERSION_STRING);
    
    TCP_CLIENT_T *state = tcp_client_init();
    if (!state)
        return;
    
    if (!tcp_client_open(state))
    {
        DEBUG_printf("run_tcp_client_test(): Could not open tcp connection.\n");
        
        tcp_result(state, -1);
        return;
    }
    
#if PICO_CYW43_ARCH_POLL
    DEBUG_printf("run_tcp_client_test(): Using poll mode\n");
#else
    DEBUG_printf("run_tcp_client_test(): Using interrupt mode\n");
#endif
    
    DEBUG_printf("run_tcp_client_test(): Entering test loop...\n");
    while(!state->complete)
    {
        // the following #ifdef is only here so this same example can be used in multiple modes;
        // you do not need it in your code
#if PICO_CYW43_ARCH_POLL
        // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
        // main loop (not from a timer) to check for WiFi driver or lwIP work that needs to be done.
        cyw43_arch_poll();
        sleep_ms(1);
#else
        // if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
        // is done via interrupt in the background. This sleep is just an example of some (blocking)
        // work you might be doing.
        sleep_ms(1000);
#endif
    }

    DEBUG_printf("Left test loop.\n");
    free(state);
}

#endif
