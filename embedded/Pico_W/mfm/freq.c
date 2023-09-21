#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#include "conf.h"
#include "freq.h"
#include "ntime.h"

#define ATMEGA_FREQ 10000000

#define UART_ID uart0
#define UART_IRQ UART0_IRQ
//#define BAUD_RATE 115200
// 156250 works for 10 Mhz clock on ATmega
#define BAUD_RATE 156250
#define DATA_BITS 8
#define STOP_BITS 1

// #define TRANSMIT_HEX
#define PARITY    UART_PARITY_NONE

// pins which go to ATmega328p
#define UART_TX_PIN 12
#define UART_RX_PIN 13

static mutex_t mutex_rx_wrt, mutex_rx_rd;

static uint8_t uart_buf[80];
static uint8_t uart_copy[80];
static int  uart_buf_p = 0, uart_buf_len;

static void on_uart_rx();

#define DEBUG_RESULT

static uint32_t rise[ MAX_MEAS ], fall[ MAX_MEAS ];

// overflow of ATmega 16 bit counter
#define OVL_CNT 65536


static uint32_t read_uart_buf( int *cnt_meas_rise, int *cnt_meas_fall, int len, int *errors);
static uint32_t read_atmega_message( const uint8_t *b, int len, int *errors);

static int last_slope = 0; // 1-> rise, 2->fall
bool collect_data_from_atmega(  meas_ctxt_t *meas_ctxt )
{
    adjust_clock();
    int cnt_meas_rise = 0;
    int cnt_meas_fall = 0;
    int cnt_both = 0;
    
    meas_ctxt->ntime_start_meas = ntime_now();

    uint8_t uart_copy[80];
    int errors = 0;
    int32_t atmega_start_meas = -1;
    int32_t atmega_end_meas = 0;
    
    while( cnt_both < 100 )
    {
        bool mok = mutex_enter_timeout_ms( &mutex_rx_wrt, 50);   // wait for writer
        if( !mok )  // timeout here means no data from ATmega
        {
            printf( "collect_data_from_atmega(): Timeout waiting for writer mutex\n\n" );
            errors++;
            continue;
        }
        
        int len = uart_buf_len;
        memcpy( uart_copy, uart_buf, len);
        uart_buf_len = 0;
        
        mutex_exit( &mutex_rx_rd );      // signal done reading
        
        if( len == 0 )
        {
            printf( "collect_data_from_atmega(): Empty buffer\n\n" );
            errors++;
        }
        else
        {
            int read_err = 0;
            uint32_t val = read_atmega_message( uart_copy, len, &read_err);
            if( read_err > 0 )
            {
                printf( "collect_data_from_atmega(): No proper value\n\n" );
                errors++;
                if( last_slope == 0 )
                {
                    rise[ cnt_meas_rise++ ] = 0;
                    fall[ cnt_meas_fall++ ] = 0;
                    cnt_both += 2;
                }
                else if( last_slope == 1 )
                {
                    fall[ cnt_meas_fall++ ] = 0;
                    cnt_both++;
                    last_slope = 2;
                }
                else
                {
                    rise[ cnt_meas_rise++ ] = 0;
                    cnt_both++;
                    last_slope = 1;
                }
            }
            else
            {
                if( atmega_start_meas == -1 )
                    atmega_start_meas = val;
                else
                    atmega_end_meas = val;
                
                const char ch = uart_copy[0];
                if( ch == '/' )
                {
                    if( last_slope == 1 )
                        printf( "collect_data_from_atmega(): wrong slope - expected fall\n" );

                    last_slope = 1;
                    //printf( "  >R %u\n", meas);
                    rise[ cnt_meas_rise++ ] = val;
                    cnt_both++;
                }
                else if( ch == '\\' )
                {
                    if( last_slope == 2 )
                        printf( "collect_data_from_atmega(): wrong slope - expected rise\n" );
                    
                    last_slope = 2;
                    //printf( "  >F %u\n", meas);
                    fall[ cnt_meas_fall++ ] = val;
                    cnt_both++;
                }
            }
        }
        
        if( errors > 5 )
            break;
    }
    
    meas_ctxt->ntime_duration = (int32_t)(ntime_now() - meas_ctxt->ntime_start_meas);
    
    if( atmega_start_meas > atmega_end_meas )
        atmega_end_meas += 10000000;
    
    meas_ctxt->cnt_meas_rise     = cnt_meas_rise;
    meas_ctxt->cnt_meas_fall     = cnt_meas_fall;
    meas_ctxt->atmega_start_meas = atmega_start_meas;
    meas_ctxt->atmega_end_meas   = atmega_end_meas;
    
    if( errors > 0 )
        printf( "collect_data_from_atmega(): %d error(s)\n\n", errors);
    
    return errors <= 5;
}


bool discard_startup_garbage_from_atmega()
{
    int read_errors = 0;
    int loops = 0;
    
    do
    {
        if( ++loops > 20 )
            return false;  // too many attempts, not ok
        
        bool mok = mutex_enter_timeout_ms( &mutex_rx_wrt, 50);   // wait for writer
        if( !mok )  // timeout here means no data from ATmega
        {
            printf( "discard_startup_garbage_from_atmega(): Timeout waiting for writer mutex\n\n" );
            continue;
        }
        
        int len = uart_buf_len;
        memcpy( uart_copy, uart_buf, len);
        uart_buf_len = 0;
        
        mutex_exit( &mutex_rx_rd );      // signal done reading
        
        read_errors = 0;
        read_atmega_message( uart_copy, len, &read_errors);
    }
    while( read_errors );

    return true;  // ok
}


static int sec_barrier_helper[ MAX_MEAS ];
static double analyze_atmega_timestamps( uint32_t *meas, int cnt_meas, uint32_t *meas_ts, uint32_t *diffs,
                                         int *errors, int *corrections, double *diff_std_dev)
{
    if( cnt_meas <= 2 )
        return -1.;
    
    int sec_barrier = 0;
    for( int n = 0; n < cnt_meas - 1; n++)
    {
        sec_barrier_helper[ n ] = sec_barrier;
        
        if( meas[ n + 1 ] < meas[ n ] )  // did we just cross a seconds barrier?
            sec_barrier++;
    }
    // do last value
    sec_barrier_helper[ cnt_meas-1 ] = sec_barrier;

    int32_t sum_diff = 0;
    int cnt_ok = 0;
    meas_ts[0] = meas[0];
    for( int n = 1; n < cnt_meas; n++)
    {
        uint32_t m = meas[ n ] + sec_barrier_helper[ n ] * ATMEGA_FREQ; // 10 Mhz on ATmega
        meas_ts[ n ] = m;
        int32_t diff = m - meas_ts[ n - 1 ];
        // printf( "%d\n", diff);

        // ignore complete outliers
        if( diff > 0 && (double)diff < (1./MAINS_FREQ * (double)ATMEGA_FREQ * 1.8) )
        {
            sum_diff += diff;
            cnt_ok++;
        }
    }

    if( sum_diff < ATMEGA_FREQ/(int)MAINS_FREQ * 20 || cnt_ok <= 30 )
    {
        printf( "Not enough diffs\n");
        return -1;
    }
    
    double avg_diff = (double)sum_diff / (double)cnt_ok;

    cnt_ok = 0;
    sum_diff = 0;
    
    diffs[0] = 0;
    
    for( int n = 1; n < cnt_meas; n++)
    {
        int32_t diff = meas_ts[ n ] - meas_ts[ n - 1 ];
        
        double diff_err = ((double)diff - avg_diff) / avg_diff * 100.;

        // ignore complete outliers
        if( diff > 0 && (double)diff < (1./MAINS_FREQ * (double)ATMEGA_FREQ * 1.8) )
        {
            // see whether there are overflow misses and if those can be corrected
            if( fabs( diff_err ) > 1.5 )  // error >1.5%?
            {
                // printf( "diff %d has error %.2f%%\n", diff, diff_err);
                
                int32_t diff_correct;
                bool try_correct = false;
                // if it's just an overflow miss or one too much, we try to correct
                if( diff_err > 15. && diff_err < 50. )
                {
                    try_correct = true;
                    diff_correct = diff - OVL_CNT;
                }
                else if( diff_err < -15. && diff_err > -50. )
                {
                    try_correct = true;
                    diff_correct = diff + OVL_CNT;
                }
                
                if( try_correct )
                {
                    // printf( "trying to correct with new value: %d\n", diff_correct);
                    double corr_err = ((double)diff_correct - avg_diff) / avg_diff * 100.;
                    if( fabs( corr_err ) < 1. )
                    {
                        // printf( "diff can be corrected, new value: %d, remaining \"error\" %.2f%%\n", diff_correct, corr_err);
                        cnt_ok++;
                        diffs[n] = diff_correct;
                        (*corrections)++;
                    }
                    else
                    {
                        printf( "diff can't be corrected, tested value: %d, remaining error %.2f%%\n", diff_correct, corr_err);
                        diffs[n] = 0;
                        (*errors)++;
                    }
                }
                else
                {
                    printf( "not trying to correct %d\n", diff);
                    diffs[n] = 0;
                    (*errors)++;
                }
            }
            else
            {
                cnt_ok++;
                diffs[n] = diff;
            }
        }
        else
        {
            printf( "outlier, not trying to correct %d\n", diff);
            (*errors)++;
            diffs[n] = 0;
        }
    }

    if( *errors > 0 )
    {
        printf( "%d errors\n", *errors);
    }
    
    double freq = -1.;

    if( cnt_ok > 30 )  // only allow it to be ok when we have enough values
    {
        sum_diff = 0;
        for( int n = 1; n < cnt_meas; n++)  // diffs[0] is always = 0
            sum_diff += diffs[n];
        
        avg_diff = (double)sum_diff / (double)cnt_ok;  // is ~200,000 for 50 Hz, ~166,667 for 60 Hz
        
        // printf( "avg diff: %f\n", avg_diff);
        freq = (double)ATMEGA_FREQ/avg_diff;   // 10,000,000 / 200,000 = 50 Hz
        
        double sum_std_dev = 0.;
        for( int n = 1; n < cnt_meas; n++)
            if( diffs[n] > 0 )
                sum_std_dev += ((double)diffs[n] - avg_diff) * ((double)diffs[n] - avg_diff);
        
        *diff_std_dev = sqrt( sum_std_dev / (double)cnt_ok );
    }
    
    return freq;
}


static void print_atmega_timestamps( uint32_t *meas, uint32_t *meas_ts, uint32_t *diffs, int cnt_meas);
static uint32_t rise_ts[ MAX_MEAS ], fall_ts[ MAX_MEAS ]; // timestamps with correct seconds barriers
static uint32_t rise_diff[ MAX_MEAS ], fall_diff[ MAX_MEAS ]; // diffs with corrections (index 0 will always be 0)

void analyze_atmega_data( const meas_ctxt_t *meas_ctxt, sample_t *sample, incident_t *incident)
{
    sample->time = 0;
    incident->time = 0;
    
    if( meas_ctxt->cnt_meas_rise <= 2 || meas_ctxt->cnt_meas_fall <= 2 )
    {
        sample->freq = -1.;
        return;
    }
    
    int rise_diff_errors = 0, rise_diff_corrections = 0;
    int fall_diff_errors = 0, fall_diff_corrections = 0;
    double rise_diff_std_dev = 0., fall_diff_std_dev = 0.;
    
    double freq_rise = analyze_atmega_timestamps( rise, meas_ctxt->cnt_meas_rise, rise_ts, rise_diff,
                                                  &rise_diff_errors, &rise_diff_corrections, &rise_diff_std_dev);
    if( freq_rise < 0. )
    {
        printf( "rise:\n" );
        print_atmega_timestamps( rise, rise_ts, rise_diff, meas_ctxt->cnt_meas_rise);
    }
    
    double freq_fall = analyze_atmega_timestamps( fall, meas_ctxt->cnt_meas_fall, fall_ts, fall_diff,
                                                  &fall_diff_errors, &fall_diff_corrections, &fall_diff_std_dev);
    if( freq_fall < 0. )
    {
        printf( "fall:\n");
        print_atmega_timestamps( fall, fall_ts, fall_diff, meas_ctxt->cnt_meas_fall);
    }
    
    double freq = -1;
    double rise_vs_fall = 0.;
    if( freq_rise > 0. && freq_fall > 0. )
    {
        freq = (freq_rise + freq_fall) / 2.;
        //printf( "freq rise: %.6f\n", freq_rise);
        //printf( "freq fall: %.6f\n", freq_fall);

        rise_vs_fall = (freq_rise - freq_fall)/freq_rise * 100.;
        //printf( "rise vs fall: %.4f%%\n", rise_vs_fall);

        //printf( "rise std dev: %.6f\n", rise_diff_std_dev);
        //printf( "fall std dev: %.6f\n", fall_diff_std_dev);
    }
    else if( freq_rise > 0. )
        freq = freq_rise;
    else if( freq_fall > 0. )
        freq = freq_fall;
    
    // printf( "=> freq : %.4f\n", freq);
    
    
    
    // ATmega timestamps are in 100ns granularity (10 Mhz clock, see F_CPU in the Makefile),
    // so devide by 10 and round to get to microseconds (us)
    bool round_up = false;
    if( meas_ctxt->atmega_start_meas % 10 >= 5 )
        round_up = true;
    
    int64_t start = meas_ctxt->atmega_start_meas/10 + (round_up ? 1:0);
    
    round_up = false;
    if( meas_ctxt->atmega_end_meas % 10 >= 5 )
        round_up = true;

    int64_t end = meas_ctxt->atmega_end_meas/10 + (round_up ? 1:0);

    int32_t meas_duration = meas_ctxt->ntime_duration;
    
    if( meas_duration > (int32_t) (1/(MAINS_FREQ - 10.) * 50 * 1000000) )
    {
        char buf[30];
        
        time_us_64_to_str( buf, 30, meas_ctxt->ntime_start_meas);
        
        printf( "%s  Measurement has implausible long duration of %d us, start = %lld, end = %lld\n", buf, meas_duration, start, end);
        printf( "rise:\n" );
        print_atmega_timestamps( rise, rise_ts, rise_diff, meas_ctxt->cnt_meas_rise);
        printf( "fall:\n");
        print_atmega_timestamps( fall, fall_ts, fall_diff, meas_ctxt->cnt_meas_fall);
    }
    
    if( meas_duration < (int32_t) (1/(MAINS_FREQ + 10.) * 50 * 1000000) )
    {
        char buf[30];
        
        time_us_64_to_str( buf, 30, meas_ctxt->ntime_start_meas);
        
        printf( "%s  Measurement has implausible short duration of %d us, start = %lld, end = %lld\n", buf, meas_duration, start, end);
        printf( "rise:\n" );
        print_atmega_timestamps( rise, rise_ts, rise_diff, meas_ctxt->cnt_meas_rise);
        printf( "fall:\n");
        print_atmega_timestamps( fall, fall_ts, fall_diff, meas_ctxt->cnt_meas_fall);
    }
    
#if defined(USE_PICOW_TIME_ONLY)
    int64_t meas_time = meas_ctxt->ntime_start_meas + (int64_t)(meas_duration/2);
#else
    // compute exact time of measurement. Use seconds granularity from picow time.
    int64_t ntime_start_meas_sec = meas_ctxt->ntime_start_meas;
    int64_t dev = ntime_start_meas_sec % 1000000;
    
    // printf( "analyze_atmega_data(): dev = %lld, start = %lld\n", dev, start);
    
    ntime_start_meas_sec -= dev;  // cut off microseconds to get to seconds granularity.
    
    // in case start from ATmega is at the beginning of a second and the picow time is at the end of a second,
    // we need to make a correction to get to the correct second.
    if( start < dev )   // are we in the "next" second?
        ntime_start_meas_sec += 1000000;
    
    // now take sub-second accuracy from the ATmega timestamps
    int64_t meas_time = ntime_start_meas_sec + (start + end) / 2;
#endif
    
    sample->time = meas_time;
    sample->freq = freq;

    // check freq plausibility
    static double lastfreq = -1.;
    static int cnt_incid = 0;
    if( lastfreq != -1. )
    {
        bool has_incid = false, meas_failed = false;
        char incid_msg[80];
        char all_msg[400];
        all_msg[0] = 0;
        
        if( freq < 0. )
        {
            has_incid = true;
            cnt_incid++;
            
            snprintf( incid_msg, 80, "INCIDENT %d: ERROR: measurement failed", cnt_incid);
            strcat( all_msg, incid_msg);

            meas_failed = true;
        }

        if( freq_rise < 0. )
        {
            if( !has_incid )
            {
                has_incid = true;
                cnt_incid++;
                snprintf( incid_msg, 80, "INCIDENT %d: ", cnt_incid);
                strcat( all_msg, incid_msg);
            }
            else
                strcat( all_msg, ",");
            snprintf( incid_msg, 80, "ERROR: rise measurement failed");
            strcat( all_msg, incid_msg);
        }
        
        if( freq_fall < 0. )
        {
            if( !has_incid )
            {
                has_incid = true;
                cnt_incid++;
                snprintf( incid_msg, 80, "INCIDENT %d: ", cnt_incid);
                strcat( all_msg, incid_msg);
            }
            else
                strcat( all_msg, ",");
            snprintf( incid_msg, 80, "ERROR: fall measurement failed");
            strcat( all_msg, incid_msg);
        }
        
        if( !meas_failed )
        {
            if( freq < INCIDENT_MAINS_FREQ_TOO_LOW )
            {
                if( !has_incid )
                {
                    has_incid = true;
                    cnt_incid++;
                    snprintf( incid_msg, 80, "INCIDENT %d: ", cnt_incid);
                    strcat( all_msg, incid_msg);
                }
                else
                    strcat( all_msg, ",");
                snprintf( incid_msg, 80, "NOTE: mains frequency of %.4f Hz too low", freq);
                strcat( all_msg, incid_msg);
            }
            else if( freq > INCIDENT_MAINS_FREQ_TOO_HIGH )
            {
                if( !has_incid )
                {
                    has_incid = true;
                    cnt_incid++;
                    snprintf( incid_msg, 80, "INCIDENT %d: ", cnt_incid);
                    strcat( all_msg, incid_msg);
                }
                else
                    strcat( all_msg, ",");
                snprintf( incid_msg, 80, "NOTE: mains frequency of %.4f Hz too high", freq);
                strcat( all_msg, incid_msg);
            }
            
            if( fabs(rise_vs_fall) > 0.005 )
            {
                if( !has_incid )
                {
                    has_incid = true;
                    cnt_incid++;
                    snprintf( incid_msg, 80, "INCIDENT %d: ", cnt_incid);
                    strcat( all_msg, incid_msg);
                }
                else
                    strcat( all_msg, ",");
                snprintf( incid_msg, 80, "ERROR: rise vs fall deviation of %.4f%% too large", rise_vs_fall);
                strcat( all_msg, incid_msg);
            }
            else if( fabs(rise_vs_fall) > 0.003 )
            {
                if( !has_incid )
                {
                    has_incid = true;
                    cnt_incid++;
                    snprintf( incid_msg, 80, "INCIDENT %d: ", cnt_incid);
                    strcat( all_msg, incid_msg);
                }
                else
                    strcat( all_msg, ",");
                snprintf( incid_msg, 80, "WARNING: rise vs fall deviation of %.4f%% large", rise_vs_fall);
                strcat( all_msg, incid_msg);
            }
        
            if( fabs( lastfreq - freq ) >= 0.01 )
            {
                if( !has_incid )
                {
                    has_incid = true;
                    cnt_incid++;
                    snprintf( incid_msg, 80, "INCIDENT %d: ", cnt_incid);
                    strcat( all_msg, incid_msg);
                }
                else
                    strcat( all_msg, ",");
                snprintf( incid_msg, 80, "WARNING: %.4f is too big of a jump", lastfreq - freq);
                strcat( all_msg, incid_msg);
            }

            if( rise_diff_std_dev >= 150 )
            {
                if( !has_incid )
                {
                    has_incid = true;
                    cnt_incid++;
                    snprintf( incid_msg, 80, "INCIDENT %d: ", cnt_incid);
                    strcat( all_msg, incid_msg);
                }
                else
                    strcat( all_msg, ",");
                snprintf( incid_msg, 80, "ERROR: rise diff stddev of %.2f is too high", rise_diff_std_dev);
                strcat( all_msg, incid_msg);
            }
        
            if( fall_diff_std_dev >= 150 )
            {
                if( !has_incid )
                {
                    has_incid = true;
                    cnt_incid++;
                    snprintf( incid_msg, 80, "INCIDENT %d: ", cnt_incid);
                    strcat( all_msg, incid_msg);
                }
                else
                    strcat( all_msg, ",");
                snprintf( incid_msg, 80, "ERROR: fall diff stddev of %.2f is too high", fall_diff_std_dev);
                strcat( all_msg, incid_msg);
            }
        
            int diff_errors = rise_diff_errors + fall_diff_errors;
            if( diff_errors > 0 )
            {
                if( !has_incid )
                {
                    has_incid = true;
                    cnt_incid++;
                    snprintf( incid_msg, 80, "INCIDENT %d: ", cnt_incid);
                    strcat( all_msg, incid_msg);
                }
                else
                    strcat( all_msg, ",");
                snprintf( incid_msg, 80, "ERROR: %d erratic diff(s)", diff_errors);
                strcat( all_msg, incid_msg);
            }

            int diff_corrections = rise_diff_corrections + fall_diff_corrections;
            if( diff_corrections > 4 )
            {
                if( !has_incid )
                {
                    has_incid = true;
                    cnt_incid++;
                    snprintf( incid_msg, 80, "INCIDENT %d: ", cnt_incid);
                    strcat( all_msg, incid_msg);
                }
                else
                    strcat( all_msg, ",");
                snprintf( incid_msg, 80, "WARNING: %d corrected diffs", diff_corrections);
                strcat( all_msg, incid_msg);
            }
        }
        
        if( has_incid )
        {
            char buf[30];
            
            time_us_64_to_str( buf, 30, meas_time);
            printf( "%s %s\n", buf, all_msg);
            
            /*printf( "%s rise:\n", buf);
            print_atmega_timestamps( rise, rise_ts, rise_diff, cnt_meas_rise);
            printf( "%s fall:\n", buf);
            print_atmega_timestamps( fall, fall_ts, fall_diff, cnt_meas_fall);
            */
            
            incident->time = meas_time;
            strncpy( incident->reason, all_msg, 250);
            incident->reason[ 249 ] = 0;
            
            memcpy( incident->rise_ts, rise_ts, meas_ctxt->cnt_meas_rise * sizeof( uint32_t ));
            incident->rise_size = meas_ctxt->cnt_meas_rise;
            memcpy( incident->fall_ts, fall_ts, meas_ctxt->cnt_meas_fall * sizeof( uint32_t ));
            incident->fall_size = meas_ctxt->cnt_meas_fall;
            
            memcpy( incident->rise_diffs, rise_diff, meas_ctxt->cnt_meas_rise * sizeof( uint32_t ));
            memcpy( incident->fall_diffs, fall_diff, meas_ctxt->cnt_meas_fall * sizeof( uint32_t ));
        }
    }

    lastfreq = freq;
}


static void print_atmega_timestamps( uint32_t *meas, uint32_t *meas_ts, uint32_t *diffs, int cnt_meas)
{
    if( cnt_meas <= 2 )
    {
        printf( "ERROR: Not enough timestamps.\n");
        return;
    }
    
    printf( "%8u %8u\n", meas[0], meas_ts[0]);
    
    int32_t min_diff = 1000000, max_diff = 0;
    int32_t sum_diff = 0;
    int cnt_ok = 0;
    for( int n = 1; n < cnt_meas; n++)
    {
        int32_t diff = meas_ts[ n ] - meas_ts[ n - 1 ];
        printf( "%8u %8u %6d %6u%s\n", meas[n], meas_ts[n], diff, diffs[n], (diffs[n] == 0) ? "  <E" : (diff == diffs[n]) ? "" : "  <C");

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
        printf( "min/max diff: %d/%d\n", min_diff, max_diff);
        double min_vs_max = ((double)min_diff - (double)max_diff) / (double)min_diff;
        printf( "min vs max diff: %.4f%%\n", min_vs_max);
        double avg_diff = (double)sum_diff / (double)cnt_ok;
        printf( "(avg diff: %f)\n", avg_diff);
        double freq = 1./(avg_diff * 1E-7);
        printf( "(freq: %.5f)\n", freq);
    }
}


void setup_freq()
{
    adj_1pps = 0;
    gpio_set_irq_callback( &gpio_callback_1pps );
    irq_set_enabled(IO_IRQ_BANK0, true);
    
    gpio_set_irq_enabled( 22, GPIO_IRQ_EDGE_RISE, true);

    mutex_init( &mutex_rx_wrt );
    mutex_init( &mutex_rx_rd );

    mutex_enter_blocking( &mutex_rx_wrt );  // start with waiting for writer
    
    // Set up a UART RX interrupt
    
    // And set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(UART_ID, true, false);
}


/*
  1PPS => GP22 (in)
*/


void init_gpios_freq()
{
    gpio_init( 22 );
    gpio_set_dir( 22, GPIO_IN);
    
    // switch SMPS to PWM mode for less noise
    gpio_init( 23 );
    gpio_set_dir( 23, GPIO_OUT);
    gpio_put( 23, 1);

    // Set up our UART with a basic baud rate.
    uart_init( UART_ID, 2400);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function( UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function( UART_RX_PIN, GPIO_FUNC_UART);

    // Actually, we want a different speed
    // The call will return the actual baud rate selected, which will be as close as
    // possible to that requested
    int actual = uart_set_baudrate( UART_ID, BAUD_RATE);
    printf( "actual baud rate = %d\n" , actual);
    

    // Set UART flow control CTS/RTS, we don't want these, so turn them off
    uart_set_hw_flow( UART_ID, false, false);

    // Set our data format
    uart_set_format( UART_ID, DATA_BITS, STOP_BITS, PARITY);

    // Turn off FIFO's - we want to do this character by character
    uart_set_fifo_enabled( UART_ID, false);
}


static int8_t hex2n( char c, int *errors)
{
    int8_t v;
    if( c >= '0' && c <= '9' )
        v = c - '0';
    else if( c >= 'A' && c <= 'F' )
        v = c - 'A' + 10;
    else
    {
        printf( "broken message - not a hex char\n" );
        (*errors)++;
    }
    return v;
}

static uint8_t hex2b( char hn, char ln, int *errors)
{
    return (hex2n( hn, errors) << 4) | hex2n( ln, errors);
}

static uint16_t hex2w( char *hw, int *errors)
{
    return ((uint16_t)hex2b( hw[0], hw[1], errors) << 8) | hex2b( hw[2], hw[3], errors);
}


static int32_t conv_hexts( char *hts, int *errors)   // 4 bytes, 8 chars
{
    return ((int32_t)hex2b( hts[0], hts[1], errors) << 24) | ((int32_t)hex2b( hts[2], hts[3], errors) << 16) |
        ((int32_t)hex2b( hts[4], hts[5], errors) << 8) | hex2b( hts[6], hts[7], errors);
}

#if defined(TRANSMIT_HEX)
static int32_t read_hexts( const uint8_t *hexchars, int *errors) // lenght must be 8 bytes = 4*2 chars
{
    char hexts[8];
    int i = 0;
    
    for( int k = 0; k < 8; k++)
        hexts[k] = (char)hexchars[ i++ ];
    
    return conv_hexts( hexts, errors);
}
#else
static uint32_t read_timestamp( const uint8_t *bytes, int *errors) // lenght must be 5 bytes, 4 bytes + 1 for checksum
{
    uint32_t a;
    uint8_t *b = (uint8_t *)&a;
    b[0] = bytes[0];
    b[1] = bytes[1];
    b[2] = bytes[2];
    b[3] = bytes[3];
    
    uint8_t chk = b[0] + b[1] + b[2] + b[3];
    if( chk != bytes[4] )
    {
        printf( "broken message - checksum error\n" );
        (*errors)++;
        a = 0;
    }
    
    return a;
}
#endif


static uint32_t read_atmega_message( const uint8_t *b, int len, int *errors)
{
    uint32_t meas = 0;
    
#if defined(TRANSMIT_HEX)
    const int tlen = 10;
#else
    const int tlen = 7;
#endif
    if( len < tlen )
    {
        printf( "Incomplete transmission\n" );
        (*errors)++;
        return 0;
    }
    
    if( b[ len - 1 ] != '\n' )
    {
        printf( "broken message - no return at end\n" );
        (*errors)++;
        return 0;
    }
    
    // see uart_send_meas() in ATmega code
    char ch = b[ 0 ];
    if( ! (ch == '/' || ch == '\\' ) )
    {
        printf( "broken message - no / or \\ at begin\n" );
        (*errors)++;
        return 0;
    }
    
    int conv_errors = 0;
#if defined(TRANSMIT_HEX)
    meas = read_hexts( b + 1, &conv_errors);
#else
    meas = read_timestamp( b + 1, &conv_errors);
#endif
    
    if( conv_errors > 0 )
    {
        printf( "broken message - errors during conversion\n" );
        *errors += conv_errors;
    }
    
    return meas;
}


// RX interrupt handler to receive data from ATmega
static uint32_t cnt_msg = 0;

uint32_t get_intr_cnt_msg()
{
    return cnt_msg;
}

static bool in_ts = false;
static int c_in_ts = 0;
static void on_uart_rx()
{
#if defined(TRANSMIT_HEX)
    const int tslen = 8;
#else
    const int tslen = 5;
#endif
    
    while( uart_is_readable( UART_ID ) )
    {
        uint8_t ch = uart_getc( UART_ID );
        
        // printf( "%c", ch);
        if( uart_buf_p < 79 )
            uart_buf[ uart_buf_p++ ] = ch;

        if( in_ts )   // make sure all of timestamp is read
        {
            c_in_ts++;
            if( c_in_ts == tslen )
                in_ts = false;
        }       
        else
        {
            if( ch == '/' || ch == '\\' )  // signature of rise/fall timestamp?
            {
                in_ts = true;
                c_in_ts = 0;
            }
            else if( ch == '\n' )
            {
                cnt_msg++;
                if( uart_buf[ 0 ] == '-' )
                {
                    uart_buf[ uart_buf_p ] = 0;
                    printf( "FROM ATMEGA: %s", uart_buf);
                }
                else
                {
                    irq_set_enabled( UART_IRQ, false);
                    uart_buf_len = uart_buf_p;
                    mutex_exit( &mutex_rx_wrt );
                    // timestamp can now be copied by core1 thread
                    
                    mutex_enter_blocking( &mutex_rx_rd );   // wait for reader
                    irq_set_enabled( UART_IRQ, true);
                }
                uart_buf_p = 0;
            }
        }
    }
}

/*
void send_test()
{
    while( 1 )
    {
        static const char *s = "123+-* /";

        for( int i = 0; i < strlen(s); i++)
        {
            if (uart_is_writable(UART_ID))
            {
                uart_putc(UART_ID, s[i]);
            }
            sleep_ms( 1000 );
        }
    }
}
*/

