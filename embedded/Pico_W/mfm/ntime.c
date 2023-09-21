#include <string.h>
#include "pico/stdlib.h"

#include "conf.h"
#include "pico/cyw43_arch.h"

#include "hardware/gpio.h"
#include "pico/multicore.h"

#include "ntime.h"


int64_t global_time_offset;

int64_t adj_1pps;

uint8_t clock_state_1pps;


void setup_clock()
{
    global_time_offset = 0LL;
    adj_1pps = 0LL;
    clock_state_1pps = CLOCK_STATE_INVALID;
}


uint8_t get_clock_state_1pps()
{
    return clock_state_1pps;
}


// interrupt callback for 1pps signal
void gpio_callback_1pps( uint gpio, uint32_t events)
{
    // printf( "gpio callback called!\n" );
    if( gpio == 22 && (events & 0x08) )  // 1pps rising edge?
    {   
        int64_t dev = ntime_now() % 1000000;
        bool state_set = false;
        
        // all values are us, 1,000,000us = 1s
        if( dev > 2 && dev < 999998 )
        {
#ifdef CLOCK_ADJ_FORWARD_ONLY
            if( dev <= 20 )
                adj_1pps = 0;
            else
#endif
            if( dev <= 1000 )
                adj_1pps = -dev;
            else if( dev >= 999000 )
                adj_1pps = 1000000 - dev;
            else
            {
#if defined(FULLY_TRUST_1PPS)
                printf( "Clock broken - extreme adj. needed, deviation = %lld\n", dev);
                clock_state_1pps = CLOCK_STATE_1PPS_EXTR_ADJ;
                state_set = true;
                
                if( dev <= 200000 )
                    adj_1pps = -dev;
                else if( dev >= 800000 )
                    adj_1pps = 1000000 - dev;
                else
                {
                    printf( "Clock broken - needs resync\n" );
                    clock_state_1pps = CLOCK_STATE_1PPS_NEED_RESY;
                    
                    if( dev <= 450000 )
                        adj_1pps = -dev;
                    else if( dev >= 550000 )
                        adj_1pps = 1000000 - dev;
                    else
                    {
                        printf( "Clock totally broken.\n" );
                        adj_1pps = 0;
                        clock_state_1pps = CLOCK_STATE_1PPS_BROKEN;
                    }
                }
#else
                printf( "Clock not adjusted since deviation of %lld too large, waiting for proper 1pps\n", dev);
                adj_1pps = 0;
                clock_state_1pps = CLOCK_STATE_1PPS_WAITING;
                state_set = true;
#endif            
            }
        }
        else  // no clock adjust needed for now
            adj_1pps = 0;

        if( !state_set )
        {
            if( adj_1pps > 100 || adj_1pps < -100 )
            {
                printf( "Clock unusual adj. by %lld\n", adj_1pps);
                clock_state_1pps = CLOCK_STATE_1PPS_UNUSU_ADJ;
            }
            else
                clock_state_1pps = CLOCK_STATE_1PPS_OK;
        }
        else
            clock_state_1pps = CLOCK_STATE_1PPS_OK;
    }
}


void adjust_clock()
{
    // adjust clock
    if( adj_1pps != 0 )
    {
        global_time_offset += adj_1pps;
        // printf( "adjust clock by %lld\n", adj_1pps);
        adj_1pps = 0;
    }
}



// buf needs to store 30 characters
int timespec2str( char *buf, uint len, struct timespec *ts, int res)
{
    int ret;
    struct tm t;
    
    if (localtime_r(&(ts->tv_sec), &t) == NULL)
        return 1;

    ret = strftime(buf, len, "%F %T", &t);
    if (ret == 0)
        return 2;
    len -= ret - 1;

    switch(res)
    {
    case 0:
        ret = snprintf( buf + strlen(buf), len, ".%09ld", ts->tv_nsec);  // ns
        break;
    case 1:
        ret = snprintf( buf + strlen(buf), len, ".%06ld", ts->tv_nsec / 1000L);  // us
        break;
    case 2:
        ret = snprintf( buf + strlen(buf), len, ".%03ld", ts->tv_nsec / 1000000L);  // ms
        break;
    default:
        return 3;
    }
    if (ret >= len)
        return 4;

    return 0;
}


// t in us
int time_us_64_to_str( char *buf, uint len, uint64_t t)
{
    struct timespec ts;
    uint64_t s = t / 1000000U;
    ts.tv_sec = s;
    ts.tv_nsec = (t - s * 1000000U) * 1000U;
    
    return timespec2str( buf, 30, &ts, 1);  // use us resolution
}



void adjust_1pps();


void client_initial_adjust_time( TCP_CLIENT_T *state )
{
    int64_t adjust;  // do not use uint64_t
    bool done = false;
    
    while( !state->error && !done )
    {
        struct timespec ts;

        uint64_t pico_t = time_us_64();
        uint32_t rtt = client_do_clsy( state, &ts);
        
        if( !state->error )
        {
            if( rtt < /* 13000 */ 9000 )
            {
                uint64_t t1 = ((uint64_t)ts.tv_sec) * 1000000U;   // sec to usec
                uint64_t t2 = ts.tv_nsec / 1000U;     // nsec to usec
                uint64_t target = t1 + t2 + rtt/2;

                char buf[30];
                time_us_64_to_str( buf, 30, target);
                printf( "%llu vs %llu\n", target, pico_t);
                printf( "target    : %s\n", buf);
                adjust = target - pico_t;
                printf( "adjust    : %lld\n", adjust);

                done = true;
            }
            else
                printf( "RTT too big\n" );
            sleep_ms( 1000 );
        }
    }

    if( !state->error && done )
    {
        global_time_offset = adjust;  // without 1PPS, subtract ~5ms here
        
        multicore_launch_core1( adjust_1pps );
        busy_wait_ms( 17000 );
        multicore_reset_core1();
    }
}


static void gpio_initial_callback_1pps( uint gpio, uint32_t events)
{
    if( gpio == 22 && (events & 0x08 ) )  // 1pps rising edge?
    {
        
        uint64_t ntime = ntime_now();
        printf( " ---------------------------------------------- MARK --------------\n" );
        //printf( "On rising edge: %llu\n", ntime);
        char buf[30];
        time_us_64_to_str( buf, 30, ntime);
        printf( "On rising edge: %s\n", buf);

        static int runs = 0;

        if( runs == 0 )
        {
            int64_t adj = ntime % 1000000;
            if( adj <= 100000 )
            {
                time_us_64_to_str( buf, 30, ntime - adj);
                printf( "Adjusted  #1  : %s  dir -\n", buf);
                adj = -adj;
            }
            else if( adj >= 900000 )
            {
                adj = 1000000 - adj;
                time_us_64_to_str( buf, 30, ntime + adj);
                printf( "Adjusted  #1  : %s  dir +\n", buf);
            }

            global_time_offset += adj;
        }
        else if( runs == 5 )
        {
            int64_t adj = ntime % 1000000;
            if( adj <= 100000 )
            {
                time_us_64_to_str( buf, 30, ntime - adj);
                printf( "Adjusted  #2  : %s  dir -\n", buf);
                adj = -adj;
            }
            else if( adj >= 900000 )
            {
                adj = 1000000 - adj;
                time_us_64_to_str( buf, 30, ntime + adj);
                printf( "Adjusted  #2  : %s  dir +\n", buf);
            }
            
            global_time_offset += adj;
        }
        
        runs++;
    }
}


void adjust_1pps()
{
    gpio_init( 22 );
    gpio_set_dir( 22, GPIO_IN);
    
    gpio_set_irq_enabled_with_callback( 22, GPIO_IRQ_EDGE_RISE, true, &gpio_initial_callback_1pps);
    
    sleep_ms( 15000 );
    
    gpio_set_irq_enabled_with_callback( 22, GPIO_IRQ_EDGE_RISE, false, 0);
}



void client_adjust_time( TCP_CLIENT_T *state )
{
    int64_t adjust;  // do not use uint64_t
    bool done = false;
    
    while( !state->error && !done )
    {
        struct timespec ts;

        uint64_t pico_t = time_us_64();
        uint32_t rtt = client_do_clsy( state, &ts);
        int64_t rtt64 = time_us_64() - pico_t;
        
        char buf[30];
        uint64_t now = ntime_now();
        time_us_64_to_str( buf, 30, now);
        printf( "Time on Pico: %s\n", buf);
        
        if( !state->error )
        {
            if( rtt < /* 13000 */ 9000 )
            {
                uint64_t t1 = ((uint64_t)ts.tv_sec) * 1000000U;   // sec to usec
                uint64_t t2 = ts.tv_nsec / 1000U;     // nsec to usec
                int64_t target = t1 + t2 + rtt/2;

                time_us_64_to_str( buf, 30, target);
                printf( "%llu vs %llu\n", target, now);
                printf( "target    : %s\n", buf);
                adjust = target - (int64_t)now;
                printf( "adjust    : %lld\n", adjust);
                
                done = true;
            }
            else
                printf( "RTT too big\n" );
            sleep_ms( 1000 );
        }
    }

    /*  if( !state->error && done )
    {
        global_time_offset = adjust;  // without 1PPS, subtract ~5ms here
    }
    */
}
