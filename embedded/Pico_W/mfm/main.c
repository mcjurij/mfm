#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/unique_id.h"

#include "conf.h"

#include "pico/cyw43_arch.h"


#include "test_tcp_client.h"
#include "tcp_client.h"

#include "freq.h"
#include "ntime.h"
#include "proto.h"
#include "ringbuffer.h"

#define DEBUG_printf printf
// #define TEST_DA

void setup_gpios();

#define MAX_SAMPLES 500
#define MAX_INCIDENTS 25

static ringbuffer_t samples, incidents;
void core1_entry();

uint8_t clock_state_comp;

int main()
{
    stdio_init_all();
    sleep_ms( 1000 );

    build_protos();
    
    ringbuffer_init( &samples, MAX_SAMPLES, sizeof(sample_t));
    ringbuffer_init( &incidents, MAX_INCIDENTS, sizeof(incident_t));
    
    /*
      pico_unique_board_id_t boardID;
      pico_get_unique_board_id(&boardID);
    */

    char idstr[ 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1 ];
    pico_get_unique_board_id_string( idstr, 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1);
    
    printf( "Pico board id: %s\n", idstr);

    pico_unique_board_id_t id;
    pico_get_unique_board_id( &id );
    unsigned int rand_seed = 0;
    for( int i = 0; i<8; i++)
        rand_seed += id.id[i];
    srand( rand_seed );

    printf( "rand seed = %u\n", rand_seed);
    
    printf("configuring pins...\n");
    setup_gpios();

#if defined(TEST_DA)
    multicore_launch_core1( core1_entry );
#endif
    
    if (cyw43_arch_init())
    {
        DEBUG_printf("failed to initialise WiFi\n");
        return 1;
    }
    
    cyw43_arch_enable_sta_mode();
    
    if( cyw43_wifi_pm(&cyw43_state, cyw43_pm_value(CYW43_NO_POWERSAVE_MODE , 20, 1, 1, 1)) == 0 )
        printf( "PM mode = no powersave for CYW43\n" );
    
    int a = 10;
    bool con = false;
    do {
        printf("Connecting to WiFi '%s' ...\n", WIFI_SSID);
        if (cyw43_arch_wifi_connect_timeout_ms( WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000))
        {
            sleep_ms( 3000 );
        }
        else
        {
            printf("Connected to WiFi.\n");
            con = true;
            break;
        }
    } while( --a > 0 );
    
    if( !con )
    {
        printf("Failed to connect. Exiting.\n");
        return 1;
    }

    TCP_CLIENT_T *state = client_open();

    if( state )
    {
        if( !client_do_ping( state ) )
        {
            printf( "ping failed - exiting\n" );
            return 1;
        }
        
        if( !client_do_idnt( state, idstr) )
        {
            printf( "sending id string failed - exiting\n" );
            return 1;
        }

        setup_clock();
        
        client_initial_adjust_time( state );
        
        multicore_launch_core1(core1_entry);  // core1 does communication with the ATmega

        int c_loops = 0;
        clock_state_comp = CLOCK_STATE_INVALID;
            
        while( true )
        {
            int rand_wait = (int)((1. + 20.) * rand() / ( RAND_MAX + 1. ) );
            sleep_ms( 1000 + rand_wait );   // wait roughly 1s, random part is to avoid wifi collision with other picow.
                                            // not sure if this really is much of a gain.
            
            c_loops++;
            if( c_loops == 4 )
            {
                c_loops = 0;
                int64_t server_time_us, pico_time_us = ntime_now();
                client_do_clcp( state, pico_time_us, &server_time_us);
                int64_t rr_time_us = ntime_now() - pico_time_us;

                if( rr_time_us < 20000 )  // 20ms
                {
                    if( llabs( pico_time_us - server_time_us ) >= 200000 )  // 200ms
                    {
                        incident_t incident;
                        incident.time = pico_time_us;
                        incident.fall_size = incident.rise_size = 0;
                        
                        char buf[80];
                        snprintf( buf, 80, "SYSTEM ERROR: Clock broken, Pico minus server time %lld us (RTT %lld)", pico_time_us - server_time_us, rr_time_us);
                        strcpy( incident.reason, buf);
                        
                        if( !producer_put( &incidents, &incident) )
                            printf( "incident buffer full!\n" );

                        clock_state_comp = CLOCK_STATE_COMP_BROKEN;
                        printf( "Clock broken, Pico minus server time %lld us (RTT %lld)\n", pico_time_us - server_time_us, rr_time_us);
                    }
                    else
                        clock_state_comp = CLOCK_STATE_COMP_OK;
                }
                else
                {
                    incident_t incident;
                    incident.time = pico_time_us;
                    incident.fall_size = incident.rise_size = 0;
                    
                    char buf[130];
                    snprintf( buf, 130, "WARNING: Clock check took too long, %lld round trip time (Pico minus server time %lld us)", rr_time_us, pico_time_us - server_time_us);
                    strcpy( incident.reason, buf);
                    
                    if( !producer_put( &incidents, &incident) )
                        printf( "incident buffer full!\n" );

                    clock_state_comp = CLOCK_STATE_COMP_TOO_LONG;
                    printf( "Clock check took too long, %lld round trip time (Pico minus server time %lld us)\n", rr_time_us, pico_time_us - server_time_us);
                }
                
                printf( "Interrupt handler count msg: %u\n", get_intr_cnt_msg());
            }
            
            // printf("--------------------------------------------\n");
            uint32_t s, d;
            double t;
            
            int c_to_send;
            consumer_get_tail( &samples, &c_to_send);
            if( c_to_send > 0 )
            {
                // printf( "Samples to send: %d\n", c_to_send);
                
                s = time_us_32();
                while( !client_do_meas( state, &samples, c_to_send) )
                {
                    // now, we have a problem. Client can't send data.
                    printf( "Urgh... client_do_meas() failed\n" );

                    client_try_reconnect( state, idstr);

                    consumer_get_tail( &samples, &c_to_send);
                }
                d = time_us_32() - s;
                t = ((double)d) / 1000000.;
                if( t > 0.01 )
                    printf( "All client_do_meas() took %.3fs\n", t);
                
                consumer_move_tail( &samples );
                c_to_send = 0;
            }
            else
            {
                printf( "No samples to send, interrupt handler count msg: %u\n", get_intr_cnt_msg());
                adjust_clock();
            }

            
            consumer_get_tail( &incidents, &c_to_send);
            if( c_to_send > 0 )
            {
                // printf( "Incidents to send: %d\n", c_to_send);
                
                s = time_us_32();
                while( !client_do_incd( state, &incidents, c_to_send) )
                {
                    // now, we have a problem. Client can't send data.
                    printf( "Urgh... client_do_incd() failed\n" );
                    
                    client_try_reconnect( state, idstr);

                    consumer_get_tail( &incidents, &c_to_send);
                }
                d = time_us_32() - s;
                t = ((double)d) / 1000000.;
                if( t > 0.01 )
                    printf( "All client_do_incd() took %.3fs\n", t);
                
                consumer_move_tail( &incidents );
                c_to_send = 0;
            }
            else
            {
                // adjust_clock();
            }
        }
    }

    
    // cyw43_arch_deinit();
    
    return 0;
}


void setup_gpios()
{
    init_gpios_freq();
}


void core1_entry()
{
    setup_freq();
    
    if( !discard_startup_garbage_from_atmega() )
    {
        printf( "Collecting data from ATmega on core1 failed!\n" );
        return;
    }
    
    printf("Collect data from ATmega on core1\n");
    uint32_t sample_number = 0;
    while( true )
    {
        meas_ctxt_t meas_ctxt;
        meas_ctxt.cnt_meas_rise = 0;
        meas_ctxt.cnt_meas_fall = 0;
        bool ok = collect_data_from_atmega( &meas_ctxt );

        incident_t incident;
        if( !ok )
        {
            incident.time = ntime_now();
            strcpy( incident.reason, "SYSTEM ERROR: No, no proper or not enough data from ATmega");
            incident.fall_size = incident.rise_size = 0;
            
            if( !producer_put( &incidents, &incident) )
                printf( "incident buffer full!\n" );
            
            continue;
        }
        
        sample_t sample;
        analyze_atmega_data( &meas_ctxt, &sample, &incident);
        sample.number = ++sample_number;
        
        if( sample.time == 0 || sample.freq < 0. )
            printf( "error during analyze data from ATmega\n" );
        else
        {
            sample.clock_state_1pps = get_clock_state_1pps();
            sample.clock_state_comp = clock_state_comp;
            
            // put sample in the ring buffer
            if( !producer_put( &samples, &sample) )
                printf( "sample buffer full!\n" );
        }

        if( incident.time != 0 )
        {
            // put incident in the ring buffer
            if( !producer_put( &incidents, &incident) )
                printf( "incident buffer full!\n" );
        }
    }
}
