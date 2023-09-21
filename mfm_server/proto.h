#ifndef _PROTO_H_
#define _PROTO_H_

#include <stdint.h>

#include "conf.h"
#ifdef PICO_CLIENT
#include "ringbuffer.h"
#endif

#define CLOCK_STATE_INVALID          0

#define CLOCK_STATE_1PPS_OK          1
#define CLOCK_STATE_1PPS_EXTR_ADJ    2
#define CLOCK_STATE_1PPS_NEED_RESY   3
#define CLOCK_STATE_1PPS_BROKEN      4
#define CLOCK_STATE_1PPS_WAITING     5
#define CLOCK_STATE_1PPS_UNUSU_ADJ  10

#define CLOCK_STATE_COMP_OK          1
#define CLOCK_STATE_COMP_BROKEN      4
#define CLOCK_STATE_COMP_TOO_LONG    5

typedef struct
{
    uint64_t  time;
    double    freq;
    uint32_t  number;
    uint8_t   clock_state_1pps;
    uint8_t   clock_state_comp;
} sample_t;


#define MAX_MEAS 60
typedef struct
{
    uint64_t  time;
    char      reason[250];
    uint32_t  rise_ts[MAX_MEAS];
    uint32_t  fall_ts[MAX_MEAS];
    uint16_t  rise_size;
    uint16_t  fall_size;
    uint32_t  rise_diffs[MAX_MEAS];  // same size as rise_ts
    uint32_t  fall_diffs[MAX_MEAS];  // same size as fall_ts
} incident_t;

typedef enum { T_INVALID = 0, T_INT16, T_UINT16, T_INT32, T_UINT32,
               T_DOUBLE,
               T_TIMESTAMP,    // 64 bit
               T_STRING,       // variable length, 16 bit length info at begin
               T_ARRAY_SAMPLE, // array of sample_t, variable length, 16 bit length info at begin
               T_ARRAY_SAMPLE_FROM_RINGBUFFER,
               T_ARRAY_UINT16,
               T_ARRAY_UINT32
} types_t;


typedef struct proto_elem
{
    int         nbr;
    types_t     type;
    struct proto_elem *next;
} proto_elem_t;


typedef struct 
{
    char   signature[8];
    char   description[40];
    proto_elem_t *first;
} proto_head_t;


typedef struct msg_elem
{
    proto_elem_t *pe;
    int           nbr;
    uint16_t      slen;  // string or array length
#ifdef PICO_CLIENT    
    ringbuffer_t *rb;   // ringbuffer to copy from
#endif
    
    union
    {
        int16_t   i16;
        uint16_t  u16;
        int32_t   i32;
        uint32_t  u32;
        double    d;
        int64_t   ts;    // timestamp
        char     *s;     // string
        void     *a;     // arrays
    } u;
    
    struct msg_elem *next;
} msg_elem_t;


typedef struct
{
    proto_elem_t *first_pe;
    proto_elem_t *pe;
    msg_elem_t   *first_me;
    msg_elem_t   *me;
    int length;
    uint8_t      *send_buffer;
} proto_ctxt_t;

void build_protos();
int read_msg( proto_ctxt_t *pc, const uint8_t *data, int data_length);
uint16_t get_elem_value_u16( proto_ctxt_t *pc, int nbr);
uint32_t get_elem_value_u32( proto_ctxt_t *pc, int nbr);
int64_t get_elem_value_ts( proto_ctxt_t *pc, int nbr);
const char *get_elem_value_s( proto_ctxt_t *pc, int nbr);
const sample_t *get_elem_array_samples( proto_ctxt_t *pc, int nbr, int *size);
const uint16_t *get_elem_array_u16( proto_ctxt_t *pc, int nbr, int *size);
const uint32_t *get_elem_array_u32( proto_ctxt_t *pc, int nbr, int *size);

void proto_free_ctxt( proto_ctxt_t *pc );
void init_proto_ctxt( proto_ctxt_t *pc, const char cmd[4]);
void init_proto_ctxt_ack( proto_ctxt_t *pc, const char cmd[8]);
void build_send_buffer( proto_ctxt_t *pc );
void append_send_buffer( proto_ctxt_t *pc1, proto_ctxt_t *pc2);

void append_msg_elem_u16( proto_ctxt_t *pc, uint16_t u16);
void append_msg_elem_u32( proto_ctxt_t *pc, uint32_t u32);
void append_msg_elem_ts( proto_ctxt_t *pc, int64_t ts);
void append_msg_elem_s( proto_ctxt_t *pc, const char *s);
void append_msg_elem_array_samples( proto_ctxt_t *pc, const sample_t *samples, int size);
#ifdef PICO_CLIENT
void append_msg_elem_array_samples_from_ringbuffer( proto_ctxt_t *pc, const ringbuffer_t *samples, int size);
#endif
void append_msg_elem_array_u16( proto_ctxt_t *pc, const uint16_t *a, int size);
void append_msg_elem_array_u32( proto_ctxt_t *pc, const uint32_t *a, int size);

#endif
