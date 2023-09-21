#ifndef _NTIME_H_
#define _NTIME_H_

#include "conf.h"

#include "tcp_client.h"

extern int64_t global_time_offset;  // all values are in us

extern int64_t adj_1pps;

void setup_clock();
uint8_t get_clock_state_1pps();

void gpio_callback_1pps( uint gpio, uint32_t events);

void adjust_clock();

int timespec2str( char *buf, uint len, struct timespec *ts, int res); // res = 0  => ns, res = 1 => us, res = 2 => ms
int time_us_64_to_str( char *buf, uint len, uint64_t t);  // t in us

void client_initial_adjust_time( TCP_CLIENT_T *state );
void client_adjust_time( TCP_CLIENT_T *state );

#define ntime_now(x) (time_us_64() + global_time_offset)

#endif
