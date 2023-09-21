#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "ringbuffer.h"


void ringbuffer_init( ringbuffer_t *rb, int size, int elem_size)
{
    rb->size = size;
    rb->elem_size = elem_size;
    rb->count = 0;
    rb->head = rb->tail = 0;
    rb->consumer_tail = 0;
    rb->consumed = 0;
    
    mutex_init( &rb->protect );
    
    rb->buffer = malloc( rb->size * rb->elem_size );
}


void ringbuffer_free( ringbuffer_t *rb )
{
    free( rb->buffer );
}


bool producer_put( ringbuffer_t *rb, void *element)
{
    mutex_enter_blocking( &rb->protect );
    if( rb->count == rb->size )
    {
        mutex_exit( &rb->protect );
        return false;  // ring buffer full
    }
    
    memcpy( rb->buffer + rb->head * rb->elem_size, element, rb->elem_size);

    rb->head++;
    if( rb->head == rb->size )
        rb->head = 0;  // wrap
    rb->count++;
    mutex_exit( &rb->protect );
    
    return true;
}


void *consumer_get( ringbuffer_t *rb )
{
    mutex_enter_blocking( &rb->protect );
    if( rb->count == 0 )
    {
        mutex_exit( &rb->protect );
        return 0;
    }
    
    void *p = rb->buffer + rb->tail * rb->elem_size;
    rb->tail++;
    if( rb->tail == rb->size )
        rb->tail = 0;  // wrap
    rb->count--;
    mutex_exit( &rb->protect );
    
    return p;  // pointer does not need to be protected
}


void *consumer_get_tail( ringbuffer_t *rb, int *nbr)
{
    mutex_enter_blocking( &rb->protect );
    if( rb->count == 0 )
    {
        mutex_exit( &rb->protect );
        *nbr = 0;
        return 0;
    }

    *nbr = rb->count;
    mutex_exit( &rb->protect );
    
    void *p = rb->buffer + rb->tail * rb->elem_size;
    rb->consumer_tail = rb->tail;
    rb->consumed = 0;
    
    return p;  // pointer does not need to be protected
}


void *consumer_get_next( ringbuffer_t *rb, int nbr)
{
    if( rb->consumed == nbr )
        return 0;
    
    void *p = rb->buffer + rb->consumer_tail * rb->elem_size;

    rb->consumer_tail++;
    if( rb->consumer_tail == rb->size )
        rb->consumer_tail = 0;  // wrap
    rb->consumed++;
    return p;
}


void consumer_move_tail( ringbuffer_t *rb )
{
    int nbr = rb->consumed;
    
    mutex_enter_blocking( &rb->protect );
    rb->count -= nbr;
    rb->tail += nbr;
    if( rb->tail >= rb->size )
        rb->tail = rb->tail - rb->size;
    mutex_exit( &rb->protect );
}
