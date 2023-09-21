#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdarg.h>

#include "conf.h"
#include "file_mgr.h"
#include "process_data.h"

static pthread_mutex_t mutex_fm;

static void file_mgr_lock();
static void file_mgr_unlock();

typedef struct {
    int id;
    char prefix[60];
    char fn[1024];
    bool can_rotate;
    FILE *fp;
} file_info_t;


static int file_id = 0;

#define MAX_FILE_INFO 40
static file_info_t file_info[ 40 ];
static char rotate_current_date[ 20 ], rotate_next_date[ 20 ];
static int64_t timestamp_next_rotate;

void init_file_mgr()
{
    pthread_mutex_init( &mutex_fm, 0);
    
    rotate_current_date[ 0 ] = rotate_next_date[ 0 ] = 0;
    
    for( int i = 0; i < MAX_FILE_INFO; i++)
    {
        file_info_t *fi = file_info + i;
        fi->id = -1;
        fi->prefix[0] = 0;
        fi->fn[0] = 0;
        fi->can_rotate = false;
        fi->fp = 0;
    }
}

static void debug_print()
{
    for( int i = 0; i < MAX_FILE_INFO; i++)
    {
        if( file_info[ i ].id != -1 )
        {
            printf( "file id: %d\n ", file_info[ i ].id );
            printf( "prefix:  %s\n", file_info[ i ].prefix);
            printf( "file n: %s\n", file_info[ i ].fn);
        }
    }
}


static int find_free_file_info()
{
    for( int i = 0; i < MAX_FILE_INFO; i++)
        if( file_info[ i ].id == -1 )
            return i;
    return -1;
}


static int find_file_info( int file_id )
{
    if( file_id < 0 )
        return -1;
    
    for( int i = 0; i < MAX_FILE_INFO; i++)
        if( file_info[ i ].id == file_id )
            return i;
    return -1;
}


void set_rotate_current_date( const char *d )
{
    strcpy( rotate_current_date, d);
}

void set_rotate_next_date( const char *d )
{
    strcpy( rotate_next_date, d);
}

void set_timestamp_next_rotate( int64_t ts )
{
    timestamp_next_rotate = ts;
}


int reg_file( const char *prefix )
{
    file_mgr_lock();

    int idx = -1, new_file_id;
    for( int i = 0; i < MAX_FILE_INFO; i++)
    {
        if( file_info[ i ].id != -1 && strcmp( file_info[ i ].prefix, prefix) == 0 )
        {
            idx = i;
            break;
        }
    }

    if( idx == -1 )
    {
        int idx = find_free_file_info();
        if( idx < 0 )
        {
            file_mgr_unlock();
            return -1;
        }
        
        file_info_t *fi = file_info + idx;
        
        fi->id = new_file_id = file_id;
        file_id++;
        
        strcpy( fi->prefix, prefix);
        
        if( OUTPUT_ROTATE_DAILY )
        {
            fi->can_rotate = true;
            snprintf( fi->fn, 1024, "%s_%s.txt", prefix, rotate_current_date);
        }
        else
        {
            fi->can_rotate = false;
            snprintf( fi->fn, 1024, "%s.txt", prefix);
        }
        
        fi->fp = fopen( fi->fn, "a");
        if( fi->fp )
            slog( "file mgr: registered and opened file '%s' with id %d, idx %d\n", fi->fn, fi->id, idx);
    }
    else
        new_file_id = file_info[ idx ].id;
    
    file_mgr_unlock();
    
    return new_file_id;
}


//  rotates file if necessary
int rotate_file( int64_t time_us, int file_id)
{
    int idx = find_file_info( file_id );
    if( idx < 0 )
        return -1;
    
    file_info_t *fi = file_info + idx;
    
    if( OUTPUT_ROTATE_DAILY )
    {
        if( time_us >= timestamp_next_rotate )
        {
            file_mgr_lock();

            if( fi->can_rotate == true )
            {
                 fi->can_rotate = false;
                 
                 if( fi->fp )
                 {
                     fclose( fi->fp );
                     snprintf( fi->fn, 1024, "%s_%s.txt", fi->prefix, rotate_next_date);
                     slog( "file mgr: rotate to new file: %s\n", fi->fn);
                     fi->fp = fopen( fi->fn, "a");
                 }
            }
            
            file_mgr_unlock();
        }
    }
    
    return idx;
}


void close_file( int file_id )
{
    file_mgr_lock();
    
    int idx = find_file_info( file_id );
    if( idx < 0 )
    {
        file_mgr_unlock();
        return;
    }

    file_info_t *fi = file_info + idx;
    if( fi->fp )
    {
        fi->id = -1;
        fi->prefix[0] = 0;
        fi->fn[0] = 0;
        fi->can_rotate = false;
        fclose( fi->fp );
        fi->fp = 0;
    }
    
    file_mgr_unlock();
}


void rotate_unrotated()
{
    if( OUTPUT_ROTATE_DAILY )
    {
        file_mgr_lock();
        
        for( int i = 0; i < MAX_FILE_INFO; i++)
            if( file_info[ i ].id != -1 && file_info[ i ].can_rotate )
            {
                file_info_t *fi = file_info + i;
                fi->can_rotate = false;

                if( fi->fp )
                {
                    fclose( fi->fp );
                    snprintf( fi->fn, 1024, "%s_%s.txt", fi->prefix, rotate_next_date);
                    slog( "file mgr: force rotate to new file: %s\n", fi->fn);
                    fi->fp = fopen( fi->fn, "a");
                }
            }
        
        file_mgr_unlock();
    }
}


void set_all_can_rotate()
{
    if( OUTPUT_ROTATE_DAILY )
    {
        for( int i = 0; i < MAX_FILE_INFO; i++)
            if( file_info[ i ].id != -1 ) 
                file_info[ i ].can_rotate = true;
    }
}


static void file_mgr_lock()
{
    pthread_mutex_lock( &mutex_fm );
}

static void file_mgr_unlock()
{
    pthread_mutex_unlock( &mutex_fm );
}


void file_mgr_fprintf( int idx, const char *format, ...)
{
    file_mgr_lock();
    file_info_t *fi = file_info + idx;

    if( fi->fp )
    {
        va_list argptr;
        va_start(argptr, format);
        vfprintf( fi->fp, format, argptr);
        va_end(argptr);
    }
    file_mgr_unlock();
}


void file_mgr_fflush( int idx )
{
    file_mgr_lock();
    file_info_t *fi = file_info + idx;
    
    if( fi->fp )
        fflush( fi->fp );
    
    file_mgr_unlock();
}
