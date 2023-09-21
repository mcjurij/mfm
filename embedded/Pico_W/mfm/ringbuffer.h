#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_

#include "conf.h"

#ifdef PICO_CLIENT
#include "pico/mutex.h"
#endif

typedef struct
{
    int size;
    int elem_size; // size of one element in bytes
    int count;  // number of elements in ring buffer, count < size
    int head;
    int tail;
    int consumer_tail;
    int consumed;
#ifdef PICO_CLIENT
    mutex_t protect;
#endif
    void *buffer;
} ringbuffer_t;

#ifdef PICO_CLIENT
void ringbuffer_init( ringbuffer_t *rb, int size, int elem_size);
void ringbuffer_free( ringbuffer_t *rb );

bool producer_put( ringbuffer_t *rb, void *element);

// void *consumer_get( ringbuffer_t *rb );
void *consumer_get_tail( ringbuffer_t *rb, int *nbr);
void *consumer_get_next( ringbuffer_t *rb, int nbr);
void consumer_move_tail( ringbuffer_t *rb );
#endif

#endif
