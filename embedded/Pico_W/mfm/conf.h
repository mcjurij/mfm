#ifndef _CONF_H_
#define _CONF_H_

#define PICO_CLIENT

#define USE_WIFI

#define USE_1PPS

// #define FULLY_TRUST_1PPS
#define CLOCK_ADJ_FORWARD_ONLY


#define MAINS_FREQ  50.
#define INCIDENT_MAINS_FREQ_TOO_LOW  (MAINS_FREQ-0.1)
#define INCIDENT_MAINS_FREQ_TOO_HIGH (MAINS_FREQ+0.1)


#endif
