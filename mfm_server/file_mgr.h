#ifndef _FILE_MGR_H_
#define _FILE_MGR_H_

void init_file_mgr();

void set_rotate_current_date( const char *d );
void set_rotate_next_date( const char *d );
void set_timestamp_next_rotate( int64_t ts );

int reg_file( const char *prefix );
FILE *get_fp( int file_id );
int rotate_file( int64_t time_us, int file_id);
void close_file( int file_id );
void rotate_unrotated();
void set_all_can_rotate();


void file_mgr_fprintf( int idx, const char *format, ...);
void file_mgr_fflush( int idx );

#endif

    
