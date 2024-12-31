#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#include "conf.h"
#include "process_data.h"
#include "conn_slots.h"
// #include "process_ringbuffer.h"
#include "file_mgr.h"

static double gridtime_offset = 0.;

void set_gridtime_offset( double offs )
{
    gridtime_offset = offs;
    slog( "setting grid time offset to: %.3f\n", offs);
}


static pthread_mutex_t mutex_signal_data;
static pthread_cond_t cond_data;

static int copy_send_id;
static int copy_samples_count;
#define MAX_COPY_SAMPLES 1000
static sample_t copy_samples[ MAX_COPY_SAMPLES ];

static int64_t timestamp_next_rotate;
static time_t time_s_next_rotate = 0;

void send_data( int send_id, const sample_t *samples, int samples_size)
{
    pthread_mutex_lock( &mutex_signal_data );
    
    copy_send_id = send_id;

    copy_samples_count = samples_size < MAX_COPY_SAMPLES ? samples_size : MAX_COPY_SAMPLES;
    memcpy( copy_samples, samples, sizeof(sample_t) * copy_samples_count);
    
    // process_data_thread() can now copy data
    pthread_cond_signal( &cond_data );
    
    pthread_mutex_unlock( &mutex_signal_data );
}


void init_process_data()
{
    pthread_mutex_init( &mutex_signal_data, 0);
    pthread_cond_init( &cond_data, 0);
    copy_samples_count = 0;
}



#define REGION_SIZE 15
#define TAIL_SIZE 7
#define INTERP_TAIL_SIZE 5

static const int region = 15;

typedef struct {
    char     idstr[32];
    int      input_count;
    sample_t input_samples[ MAX_COPY_SAMPLES ];
    int      tail_count;
    int64_t  times[ TAIL_SIZE ]; // in us
    double   freqs[ TAIL_SIZE ];
    
    int      interp_count;
    int64_t  interp_times[ INTERP_TAIL_SIZE ]; // in us
    double   interp_freqs[ INTERP_TAIL_SIZE ];
    int64_t  old_start_second;
    
    bool     first_second;
    int      interp_region_count;
    time_t   interp_region_times[ REGION_SIZE ];
    double   interp_region_freqs[ REGION_SIZE ];
    
    int      file_meas_id;
    int      file_meas_local_id;
    int      file_sg_id;
    int      file_sg_local_id;    
} process_slot_t;

static  process_slot_t process_slots[ MAX_CONN_SLOT ];


typedef struct {
    time_t time;
    int    freq_count;
    double freq[ MAX_CONN_SLOT ];
} time_freq_pair_t;

#define MERGE_WINDOW_SIZE 20

typedef struct {
    time_t   window_start_time;

    int      merge_window_count;
    time_freq_pair_t   merge_window[ MERGE_WINDOW_SIZE ];

    char     fn_file_meas[1024];
    int      file_meas_id;

    bool     write_gridtime;
    double   max_allowed_difference;
} merge_data_t;

static  merge_data_t merge_data, merge_sgfit;

typedef struct {
    time_t   ref_second;
    time_t   prev_second;
    int64_t  accumulate;
    int      file_id;
    int      file_local_id;
} gridtime_data_t;

static gridtime_data_t gridtime_data;


static void reset_process_slot( process_slot_t *process_slot );
static void init_process_slots();

static void meas_write_data( process_slot_t *process_slot );
static void write_region( process_slot_t *process_slot );

static void init_merge_data_inter();
static void init_merge_data_sgfit();
static void write_merge( merge_data_t *md, time_t time, double freq);

static void init_gridtime_data();
static void write_gridtime( gridtime_data_t *gtd, time_t time, double freq);


static int64_t conv2us( time_t t );

static void init_rotate()
{
    time_t current_time_s;
    
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    current_time_s = spec.tv_sec;
    
    struct tm utc_tm;
    gmtime_r( &current_time_s, &utc_tm);
    
    utc_tm.tm_sec = 0;
    utc_tm.tm_min = 0;
    utc_tm.tm_hour = 0;
    utc_tm.tm_isdst = -1;
    time_t day_start = mktime( &utc_tm );
    time_t day_end   = day_start + 24*60*60;
    
    time_s_next_rotate = day_end;
    timestamp_next_rotate = conv2us( day_end );
    set_timestamp_next_rotate( timestamp_next_rotate );
    
    struct tm local_tm;
    char rotate_current_date[ 20 ], rotate_next_date[ 20 ];
    localtime_r( &day_start, &local_tm);
    strftime( rotate_current_date, 20, "%F", &local_tm);
    localtime_r( &day_end, &local_tm);
    strftime( rotate_next_date, 20, "%F", &local_tm);
    
    slog( "initial: current %s next %s\n", rotate_current_date, rotate_next_date);
    
    set_rotate_current_date( rotate_current_date );
    set_rotate_next_date( rotate_next_date );
    set_all_can_rotate();
}


static void check_rotate()
{
    time_t current_time_s;
    
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    current_time_s = spec.tv_sec;
    
    if( current_time_s > (time_s_next_rotate + 5 * 60)  )
    {
        rotate_unrotated();
        
        time_t day_start = time_s_next_rotate;
        time_s_next_rotate += 24 * 60 * 60;
        time_t day_end = time_s_next_rotate;
        
        timestamp_next_rotate = conv2us( day_end );
        set_timestamp_next_rotate( timestamp_next_rotate );
            
        struct tm local_tm;
        char rotate_current_date[ 20 ], rotate_next_date[ 20 ];
        localtime_r( &day_start, &local_tm);
        strftime( rotate_current_date, 20, "%F", &local_tm);
        localtime_r( &day_end, &local_tm);
        strftime( rotate_next_date, 20, "%F", &local_tm);
            
        slog( "rotate: current %s next %s\n", rotate_current_date, rotate_next_date);

        set_rotate_current_date( rotate_current_date );
        set_rotate_next_date( rotate_next_date );
        set_all_can_rotate();
    }
}

// thread routine to process data
void *process_data_thread(void *)
{
    int conn_id;
    int samples_count;
    
    init_process_slots();
    init_file_mgr();
    
    if( OUTPUT_ROTATE_DAILY )
    {
        init_rotate();
    }
    
    init_merge_data_inter();
    init_merge_data_sgfit();
    init_gridtime_data();
    gridtime_data.file_id = reg_file( "gridtime" );
    gridtime_data.file_local_id = reg_file( "gridtime_local" );
    
    int check_rotate_cnt = 0;
    
    do {
        pthread_mutex_lock( &mutex_signal_data );

        while( copy_samples_count == 0 ) {
            // printf("process_data_thread(): Going into wait...\n" );
            pthread_cond_wait( &cond_data, &mutex_signal_data);
            // printf("process_data_thread(): Condition signal received. samples count = %d\n", copy_samples_count);
        }
        conn_id = copy_send_id;
        
        samples_count = copy_samples_count;
        
        memcpy( process_slots[ conn_id ].input_samples, copy_samples, sizeof(sample_t) * copy_samples_count);
        copy_samples_count  = 0;
        
        pthread_mutex_unlock( &mutex_signal_data );    // done reading
        
        process_slots[ conn_id ].input_count = samples_count;
        
        char *conn_idstr = conn_slots[ conn_id ].idstr;
        // slog( "process_data_thread(): From %s (%d), come %d samples\n", conn_idstr, conn_id, samples_count);
        
        if( strcmp( process_slots[ conn_id ].idstr, conn_idstr) != 0 )
        {
            if( process_slots[ conn_id ].file_meas_id != -1 )
                close_file( process_slots[ conn_id ].file_meas_id );
            if( process_slots[ conn_id ].file_meas_local_id != -1 )
                close_file( process_slots[ conn_id ].file_meas_local_id );
            if( process_slots[ conn_id ].file_sg_id != -1 )
                close_file( process_slots[ conn_id ].file_sg_id );
            if( process_slots[ conn_id ].file_sg_local_id != -1 )
                close_file( process_slots[ conn_id ].file_sg_local_id );
            
            reset_process_slot( process_slots + conn_id );
            
            strcpy( process_slots[ conn_id ].idstr, conn_idstr);

            char fn[60];
            
            snprintf( fn, 60, "meas_data_%s", conn_idstr);
            process_slots[ conn_id ].file_meas_id = reg_file( fn );
                        
            snprintf( fn, 60, "meas_data_local_%s", conn_idstr);
            process_slots[ conn_id ].file_meas_local_id = reg_file( fn );

            snprintf( fn, 60, "meas_sgfit_%s", conn_idstr);
            process_slots[ conn_id ].file_sg_id = reg_file( fn );
            
            snprintf( fn, 60, "meas_sgfit_local_%s", conn_idstr);
            process_slots[ conn_id ].file_sg_local_id = reg_file( fn );
        }
        
        meas_write_data( process_slots + conn_id );
        write_region( process_slots + conn_id );
        
        check_rotate_cnt++;
        if( OUTPUT_ROTATE_DAILY && check_rotate_cnt == 500 )
        {
            check_rotate();
            check_rotate_cnt = 0;
        }
        
    } while( true );

    return 0;
}


static void meas_write_data( process_slot_t *process_slot )
{
    int i;
    const char *idstr = process_slot->idstr;
    
    if( process_slot->input_count > 4 )
        slog( "%s - %d measurements\n", idstr, process_slot->input_count);
    
    const sample_t *samples = process_slot->input_samples;

    int idx, idx_local;
    
    for( i = 0; i < process_slot->input_count; i++)
    {
        idx = rotate_file( samples[i].time, process_slot->file_meas_id);
        file_mgr_fprintf( idx, "%lu  %.6f  %u  %u %u\n", samples[i].time, samples[i].freq, samples[i].number,
                 (unsigned int)samples[i].clock_state_1pps, (unsigned int)samples[i].clock_state_comp);
        
        idx_local = rotate_file( samples[i].time, process_slot->file_meas_local_id);
        char buf[30];
        time_us_64_to_str( buf, 30, samples[i].time);
        file_mgr_fprintf( idx_local, "%s,%.6f\n", buf, samples[i].freq);
    }

    if( process_slot->input_count > 0 )
    {
        file_mgr_fflush( idx );
        file_mgr_fflush( idx_local );
    }
}


static int64_t conv2us( time_t t )
{
    return (int64_t)t * 1000000LL;
}

static int interpolate_tail( int64_t start_second, int64_t end_second, process_slot_t *process_slot);
static double interpolate( int64_t at_second, int64_t time0, double freq0, int64_t time1, double freq1);
static double sgfit( const double *y );

// write out tail and the smoothed curve using sgfit
static void write_region( process_slot_t *process_slot )
{
    for( int n = 0; n < process_slot->input_count; n++)
    {
        int64_t time = process_slot->input_samples[ n ].time;
        double  freq = process_slot->input_samples[ n ].freq;
        
        if( process_slot->tail_count < TAIL_SIZE )
        {
            process_slot->times[ process_slot->tail_count ] = time;
            process_slot->freqs[ process_slot->tail_count ] = freq;
            process_slot->tail_count++;
        }
        else
        {
            memmove( process_slot->times, process_slot->times + 1, sizeof(int64_t) * (TAIL_SIZE-1));
            memmove( process_slot->freqs, process_slot->freqs + 1, sizeof(double) * (TAIL_SIZE-1));
            process_slot->times[ TAIL_SIZE-1 ] = time;
            process_slot->freqs[ TAIL_SIZE-1 ] = freq;
            
            int64_t interp_start,interp_end;

            interp_start = process_slot->times[ 0 ] - process_slot->times[ 0 ] % 1000000LL;
            interp_end = process_slot->times[ TAIL_SIZE-1 ] - process_slot->times[ TAIL_SIZE-1 ] % 1000000LL;
            int pts = interpolate_tail( interp_start, interp_end, process_slot);

            /*
            if( process_slot->interp_count > 0 )
            {
                printf( "   -> pts = %d\n", pts);
                for( int n = 0; n < process_slot->interp_count; n++)
                {
                    int64_t second = process_slot->interp_times[ n ]; // / 1000000LL;
                    double  int_freq =  process_slot->interp_freqs[ n ];
            
                    printf( "   -> %d: %jd  %f\n", n, second, int_freq);
                }
            }
            else
                printf( "  nothing\n");
            */
            
            if( pts > 0 )
            {
                if( process_slot->first_second  )
                {
                    process_slot->first_second = false;
                    process_slot->old_start_second = process_slot->interp_times[ 0 ];
                }
                else if( process_slot->old_start_second < process_slot->interp_times[ 0 ] )
                {
                    process_slot->old_start_second = process_slot->interp_times[ 0 ];
                    
                    int64_t second_ts = process_slot->interp_times[ 0 ];
                    time_t second = (time_t) (second_ts / 1000000LL);
                    double  freq_inter =  process_slot->interp_freqs[ 0 ];
                    printf( "   -> %s: %jd  %f\n", process_slot->idstr, second_ts, freq_inter);
                    
                    write_merge( &merge_data, second, freq_inter);
                    
                    process_slot->interp_region_times[ process_slot->interp_region_count ] = second;
                    process_slot->interp_region_freqs[ process_slot->interp_region_count ] = freq_inter;
                    process_slot->interp_region_count++;
                    
                    if( process_slot->interp_region_count == REGION_SIZE )  // region buffer full?
                    {
                        // FIXME we need to check whether the time stamps are contiguous
                        time_t s = process_slot->interp_region_times[ REGION_SIZE/2 ];
                        struct tm t;
                        localtime_r( &s, &t);
                        char buf[30];
                        strftime( buf, 30, "%F %T", &t);
                        double fitted = sgfit( process_slot->interp_region_freqs );
                        
                        write_merge( &merge_sgfit, s, fitted);
                        
                        int idx, idx_local;
                        idx = rotate_file( conv2us( s ), process_slot->file_sg_id);
                        idx_local = rotate_file( conv2us( s ), process_slot->file_sg_local_id);
                        file_mgr_fprintf( idx, "%ld  %.6f\n", conv2us( s ), fitted);
                        file_mgr_fprintf( idx_local, "%s,%.6f\n", buf, fitted);

                        file_mgr_fflush( idx );
                        file_mgr_fflush( idx_local );
                        
                        process_slot->interp_region_count--;
                        memmove( process_slot->interp_region_times, process_slot->interp_region_times + 1, sizeof(time_t) * (REGION_SIZE - 1));
                        memmove( process_slot->interp_region_freqs, process_slot->interp_region_freqs + 1, sizeof(double) * (REGION_SIZE - 1));
                    }
                }
            }
        }
    }
}


static void reset_process_slot( process_slot_t *process_slot )
{
    process_slot->idstr[0] = 0;
    process_slot->tail_count = 1;
    process_slot->times[0] = 0LL;
    process_slot->freqs[0] = 0.;
    process_slot->old_start_second = 0LL;
    process_slot->interp_region_count = 0;
    process_slot->first_second = true;
    process_slot->file_meas_id = -1;
    process_slot->file_meas_local_id = -1;
    process_slot->file_sg_id = -1;
    process_slot->file_sg_local_id = -1;
}


static void init_process_slot( process_slot_t *process_slot )
{
    reset_process_slot( process_slot );
    
    process_slot->input_count = 0;
}


static void init_process_slots()
{
    for( int i = 0; i < MAX_CONN_SLOT; i++)
    {
        process_slot_t *process_slot = process_slots + i;

        init_process_slot( process_slot );
    }
}


static int interpolate_tail( int64_t start_second, int64_t end_second, process_slot_t *process_slot)
{
    int pts = 0;
    int idx = 0;
    process_slot->interp_count = 0;
    
    for( int64_t sec = start_second; sec <= end_second; )
    {
        int64_t time0, time1;
        double  freq0, freq1;
    
        time0 = process_slot->times[ idx ]; // in us
        freq0 = process_slot->freqs[ idx ];
        do {
            if( sec < time0 )
            {
                sec += 1000000LL;
                //printf( "adjust by 1 sec\n");
            }
            else
                break;
        } while( 1 );
    
        time1 = process_slot->times[ idx + 1 ];
        freq1 = process_slot->freqs[ idx + 1 ];
    
    
        //printf( "0: %jd  %.6f\n", time0, freq0);
        //printf( "   %jd\n", sec);
        //printf( "1: %jd  %.6f\n", time1, freq1);
        assert( time0 <= sec );
        if( sec < time1 )
        {
            double interp_freq = interpolate( sec, time0, freq0, time1, freq1);
            // printf( "   f = %.6f\n", interp_freq);
        
            process_slot->interp_times[ process_slot->interp_count ] = sec;
            process_slot->interp_freqs[ process_slot->interp_count ] = interp_freq;
            pts++;
            process_slot->interp_count++;
            if( process_slot->interp_count == INTERP_TAIL_SIZE )
                break;
        }
    
        if( idx < TAIL_SIZE-1 )
            idx++;
        else
            break;
    }
    return pts;
}


static double interpolate( int64_t at_second, int64_t time0, double freq0, int64_t time1, double freq1)
{
    // precondition time0 <= at_second && at_second < time1
    
    double slope = (freq1 - freq0) / (double) (time1 - time0);

    return freq0 + slope * (double)(at_second - time0);
}


// Savitzky–Golay filter with fixed window size of 15
static double sgfit( const double *y )
{
    // see table from http://www.statistics4u.info/fundstat_eng/cc_savgol_coeff.html
    static const double sgfilter_coeff[ REGION_SIZE ] = {
        -78, -13, 42, 87, 122, 147, 162,
        167,
        162, 147, 122, 87, 42, -13, -78
    };
    
    double sum = 0.;
    for( size_t i = 0; i < region; i++)
        sum += sgfilter_coeff[ i ] * y[ i ];
    
    return sum / 1105.;  // h value from table see comment above
}


static void init_merge_data( merge_data_t *md )
{
    md->window_start_time = 0;
    md->merge_window_count = 0;
    md->file_meas_id = -1;
    
    for( int i = 0; i < MERGE_WINDOW_SIZE; i++)
    {
        md->merge_window[ i ].time = 0;
        md->merge_window[ i ].freq_count = 0;
        
        for( int j = 0; j < MAX_CONN_SLOT; j++)
            md->merge_window[ i ].freq[ j ] = 0.;
    }

    md->write_gridtime = false;
}


static void init_merge_data_inter()
{
    init_merge_data( &merge_data );
    
    merge_data.file_meas_id = reg_file( "meas_merge" );
    merge_data.write_gridtime = false;
    merge_data.max_allowed_difference = 0.004;
}

static void init_merge_data_sgfit()
{
    init_merge_data( &merge_sgfit );
    
    merge_sgfit.file_meas_id = reg_file( "meas_merge_sgfit" );
    merge_sgfit.write_gridtime = true;
    merge_sgfit.max_allowed_difference = 0.0006;
}


static void write_merge( merge_data_t *md, time_t time, double freq)
{
    if( md->window_start_time == 0 )
    {
        // init
        md->window_start_time = time;
        md->merge_window[ 0 ].time = time;
        md->merge_window[ 0 ].freq_count = 1;
        md->merge_window[ 0 ].freq[ 0 ] = freq;
        md->merge_window_count = 1;
        return;
    }
    else if( time < md->window_start_time )
        return;  // simply ignore
    
    bool found = false;
    for( int i = 0 ; !found && i < md->merge_window_count; i++)
        if( md->merge_window[ i ].time == time )
        {
            found = true;
            if(  md->merge_window[ i ].freq_count < MAX_CONN_SLOT )
            {
                md->merge_window[ i ].freq[ md->merge_window[ i ].freq_count ] = freq;
                md->merge_window[ i ].freq_count++;
            }
        }

    if( !found )
    {
        if( md->merge_window_count == MERGE_WINDOW_SIZE )  // window full?
        {
            double sum = 0., avg;
            
            for( int i = 0; i < md->merge_window[ 0 ].freq_count; i++)
                sum += md->merge_window[ 0 ].freq[i];
            
            avg = sum / (double)md->merge_window[ 0 ].freq_count;

            for( int i = 0; i < md->merge_window[ 0 ].freq_count; i++)
                if( fabs( avg - md->merge_window[ 0 ].freq[i] ) > md->max_allowed_difference )
                    slog( "WARNING: during merge: difference of %.5f out of allowed range\n", avg - md->merge_window[ 0 ].freq[i]);
            
            int fidx = rotate_file( conv2us( md->merge_window[ 0 ].time ), md->file_meas_id);
            file_mgr_fprintf( fidx, "%ld  %.6f\n", conv2us( md->merge_window[ 0 ].time ), avg);
            file_mgr_fflush( fidx );

            if( md->write_gridtime )
                write_gridtime( &gridtime_data, md->merge_window[ 0 ].time, avg);
            
            md->merge_window_count--;
            memmove( md->merge_window, md->merge_window + 1, sizeof( time_freq_pair_t ) * md->merge_window_count);
            md->window_start_time = md->merge_window[ 0 ].time;
        }
        
        int idx;
        for( idx = 0; idx < md->merge_window_count; idx++)
            if( time < md->merge_window[ idx ].time )
                break;

        if( idx < md->merge_window_count ) // not at the end, but somewhere in between?
            memmove( md->merge_window + idx + 1, md->merge_window + idx, sizeof( time_freq_pair_t ) * (md->merge_window_count - idx));
        
        md->merge_window[ idx ].time = time;
        md->merge_window_count++;
        md->merge_window[ idx ].freq_count = 1;
        md->merge_window[ idx ].freq[ 0 ] = freq;
    }
    
    int n;
    for( n = 0; n < md->merge_window_count; n++)
    {
        if( md->merge_window[ n ].freq_count >= 2 )
        {
            double sum = 0., avg;
            
            for( int i = 0; i < md->merge_window[ n ].freq_count; i++)
                sum += md->merge_window[ n ].freq[i];
            
            avg = sum / (double)md->merge_window[ n ].freq_count;

            for( int i = 0; i < md->merge_window[ n ].freq_count; i++)
                if( fabs( avg - md->merge_window[ n ].freq[i] ) > md->max_allowed_difference )
                    slog( "WARNING: during merge: difference of %.5f out of allowed range\n", avg - md->merge_window[ n ].freq[i]);
            
            int fidx = rotate_file( conv2us( md->merge_window[ n ].time ), md->file_meas_id);
            file_mgr_fprintf( fidx, "%ld  %.6f\n", conv2us( md->merge_window[ n ].time ), avg);
            file_mgr_fflush( fidx );
            
            if( md->write_gridtime )
                write_gridtime( &gridtime_data, md->merge_window[ n ].time, avg);
        }
        else
            break;
    }
    
    if( n > 0 )
    {
         memmove( md->merge_window, md->merge_window + n, sizeof( time_freq_pair_t ) * (md->merge_window_count - n));
         md->merge_window_count -= n;
         md->window_start_time = md->merge_window[ 0 ].time;
    }
}


static void init_gridtime_data()
{
    gridtime_data.ref_second = 0L;
    gridtime_data.prev_second = 0L;
    if( gridtime_offset != 0. )
        gridtime_data.accumulate = (int64_t)llround( 1000000. * gridtime_offset );
    else
        gridtime_data.accumulate = 0LL;
    gridtime_data.file_id = -1;
    gridtime_data.file_local_id = -1;
}


static void write_gridtime( gridtime_data_t *gtd, time_t time, double freq)
{
    int idx = rotate_file( conv2us( time ), gtd->file_id);
    int idx_local = rotate_file( conv2us( time ), gtd->file_local_id);
    
    if( gtd->ref_second == 0 )
    {
        gtd->ref_second = time;
        gtd->prev_second = time;
        gtd->accumulate += (int64_t)llround( 1000000. * (freq / MAINS_FREQ) );
        double it = (double)gtd->accumulate / 1000000.;
            
        file_mgr_fprintf( idx, "# restart\n%ld  %.3f\n", time, it);

        struct tm t;
        localtime_r( &time, &t);
        char buf[30];
        strftime( buf, 30, "%F %T", &t);
            
        file_mgr_fprintf( idx_local, "# restart\n%s,%.3f\n", buf, it);
    }
    else
    {
        if( time - gtd->prev_second == 1 )
        {
            gtd->accumulate += (int64_t)llround( 1000000. * (freq / MAINS_FREQ) );
            double gt = (double)gtd->accumulate / 1000000. - (double)(time - gtd->ref_second + 1);
            
            file_mgr_fprintf( idx, "%ld  %.3f\n", time, gt);
            
            struct tm t;
            localtime_r( &time, &t);
            char buf[30];
            strftime( buf, 30, "%F %T", &t);
            
            file_mgr_fprintf( idx_local, "%s,%.3f\n", buf, gt);
        }
        else
        {
            file_mgr_fprintf( idx, "# data hole at %ld - %ld\n", gtd->prev_second, time);

            struct tm t;
            localtime_r( &time, &t);
            char buf[30];
            strftime( buf, 30, "%F %T", &t);
            
            localtime_r( &(gtd->prev_second), &t);
            char pbuf[30];
            strftime( pbuf, 30, "%F %T", &t);
            
            file_mgr_fprintf( idx_local, "# data hole at %s - %s\n", pbuf, buf);
            
            gtd->accumulate += 1000000LL * (time - gtd->prev_second);
        }
        
        gtd->prev_second = time;
    }
    
    file_mgr_fflush( idx );
    file_mgr_fflush( idx_local );
}


// buf needs to store 30 characters
int timespec2str( char *buf, int len, struct timespec *ts, int res)
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


int timespec2str_ms( char *buf, int len, struct timespec *ts)
{
    return timespec2str( buf, len, ts, 2);
}


int time_us_64_to_str( char *buf, int len, uint64_t t)
{
    struct timespec ts;
    uint64_t s = t / 1000000U;
    ts.tv_sec = s;
    ts.tv_nsec = (t - s * 1000000U) * 1000U;
    
    return timespec2str( buf, len, &ts, 1);
}


void slog( const char *format, ...)
{
    static FILE *logf = 0;

    if( !logf )
    {
        logf = fopen( "log.txt", "a");
        if( !logf )
        {
            fprintf(stderr, "Can't open log file. Exiting.\n" );
            exit(1);
        }
    }
    
    char timestr[30];
    struct timespec spec;
    clock_gettime( CLOCK_REALTIME, &spec);
    timespec2str_ms( timestr, 30, &spec);
    fprintf( logf, "%s  ", timestr);
    
    va_list argptr;
    va_start(argptr, format);
    vfprintf( logf, format, argptr);
    va_end(argptr);
    fflush( logf );
}
