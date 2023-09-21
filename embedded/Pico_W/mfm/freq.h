#ifndef FREQ_H_
#define FREQ_H_

#include <stdint.h>
#include "proto.h"

typedef struct {
    int64_t ntime_start_meas;
    int32_t ntime_duration;
    int cnt_meas_rise;
    int cnt_meas_fall;
    int32_t atmega_start_meas;
    int32_t atmega_end_meas;
} meas_ctxt_t;

bool collect_data_from_atmega( meas_ctxt_t *meas_ctxt );

bool discard_startup_garbage_from_atmega();
void analyze_atmega_data( const meas_ctxt_t *meas_ctxt, sample_t *sample, incident_t *incident);

void setup_freq();
void init_gpios_freq();
uint32_t get_intr_cnt_msg();

#endif
