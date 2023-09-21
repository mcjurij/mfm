#ifndef _PROCESS_DATA_H_
#define _PROCESS_DATA_H_

#include <time.h>

#include "proto.h"

void set_gridtime_offset( double offs );

void send_data( int send_id, const sample_t *samples, int samples_size);

void init_process_data();

// thread routine to process data
void *process_data_thread(void *);

int timespec2str_ms( char *buf, int len, struct timespec *ts);
int time_us_64_to_str( char *buf, int len, uint64_t t);

void slog( const char *format, ...);

#endif
