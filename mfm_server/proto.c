#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "proto.h"

static void *proto_malloc( int size )
{
    void *p = malloc( size );
    if( p == 0 )
        printf( "proto_malloc(): Out of memory!\n\n\n" );
    // printf( "  malloc'd: %p\n", p);
    return p;
}

static void *proto_realloc( void *op, int size )
{
    void *p = realloc( op, size);
    if( p == 0 )
        printf( "proto_realloc(): Out of memory!\n\n\n" );
    // printf( " realloc'd: %p (was %p)\n", p, op);
    return p;
}

static char *proto_strdup( const char *s )
{
    char *p = strdup( s );
    if( p == 0 )
        printf( "proto_strdup(): Out of memory!\n\n\n" );
    // printf( "   alloc'd: %p\n", p);
    return p;
}


static void proto_free( void *p )
{
    // printf( "      free: %p\n", p);
    if( p )
        free( p );
}

static proto_head_t proto_msg[50];
static proto_head_t proto_msg_ack[50];


void build_protos()
{
    int c_msg = 0;
    proto_elem_t *pe;

    // client -> server
    strcpy( proto_msg[ c_msg ].description, "MEASUREMENTS");
    memcpy( proto_msg[ c_msg ].signature, "MEAS", 4);
    
    pe = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    proto_msg[ c_msg ].first = pe;
    pe->nbr = 1;
    pe->type = T_ARRAY_SAMPLE_FROM_RINGBUFFER;
    
    pe->next = 0;

    c_msg++;
    strcpy( proto_msg[ c_msg ].description, "PICO IDENT STRING");
    memcpy( proto_msg[ c_msg ].signature, "IDNT", 4);
    
    pe = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    proto_msg[ c_msg ].first = pe;
    pe->nbr = 1;
    pe->type = T_STRING;
    
    pe->next = 0;

    c_msg++;
    strcpy( proto_msg[ c_msg ].description, "INCIDENT CONTAINER");
    memcpy( proto_msg[ c_msg ].signature, "INC0", 4);
    
    pe = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    proto_msg[ c_msg ].first = pe;
    pe->nbr = 1;
    pe->type = T_UINT16;                // number of incidents to follow
    
    pe->next = 0;
    
    c_msg++;
    strcpy( proto_msg[ c_msg ].description, "INCIDENT");
    memcpy( proto_msg[ c_msg ].signature, "INC1", 4);
    
    pe = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    proto_msg[ c_msg ].first = pe;
    pe->nbr = 1;
    pe->type = T_TIMESTAMP;
    
    pe->next = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    pe = pe->next;
    pe->nbr = 2;
    pe->type = T_STRING;

    pe->next = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    pe = pe->next;
    pe->nbr = 3;
    pe->type = T_ARRAY_UINT32;            // rise timestamps from ATmega

    pe->next = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    pe = pe->next;
    pe->nbr = 4;
    pe->type = T_ARRAY_UINT32;            // fall timestamps from ATmega
    
    pe->next = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    pe = pe->next;
    pe->nbr = 5;
    pe->type = T_ARRAY_UINT32;            // rise diffs with corrections computed by PicoW

    pe->next = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    pe = pe->next;
    pe->nbr = 6;
    pe->type = T_ARRAY_UINT32;            // fall diffs with corrections computed by PicoW
    
    pe->next = 0;
    
    c_msg++;
    strcpy( proto_msg[ c_msg ].description, "CLOCK COMPARE");
    memcpy( proto_msg[ c_msg ].signature, "CLCP", 4);
    
    pe = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    proto_msg[ c_msg ].first = pe;
    pe->nbr = 1;
    pe->type = T_TIMESTAMP;
    
    pe->next = 0;
    
    c_msg++;
    proto_msg[ c_msg ].signature[0] = 0;

    // ACKs are server->client
    int c_msg_ack = 0;

    strcpy( proto_msg_ack[ c_msg_ack ].description, "CLOCK SYNC ACK");
    memcpy( proto_msg_ack[ c_msg_ack ].signature, "CLSY_ACK", 8);
    
    pe = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    proto_msg_ack[ c_msg_ack ].first = pe;
    pe->nbr = 1;
    pe->type = T_TIMESTAMP;
    
    pe->next = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    pe = pe->next;
    pe->nbr = 2;
    pe->type = T_UINT32;
    
    pe->next = 0;
    
    c_msg_ack++;
    strcpy( proto_msg_ack[ c_msg_ack ].description, "CLOCK COMPARE ACK");
    memcpy( proto_msg_ack[ c_msg_ack ].signature, "CLCP_ACK", 8);
    
    pe = (proto_elem_t *)malloc( sizeof(proto_elem_t) );
    proto_msg_ack[ c_msg_ack ].first = pe;
    pe->nbr = 1;
    pe->type = T_TIMESTAMP;
        
    pe->next = 0;

    c_msg_ack++;
    proto_msg_ack[ c_msg_ack ].signature[0] = 0;
}


static proto_elem_t *find_proto( const char cmd[4] )
{
    for( int i = 0; i < 50 && proto_msg[i].signature[0] != 0; i++)
        if( strncmp( cmd, proto_msg[i].signature, 4) == 0 )
            return proto_msg[i].first;
    return 0;
}


static proto_elem_t *find_proto_ack( const char cmd[8] )
{
    for( int i = 0; i < 50 && proto_msg_ack[i].signature[0] != 0; i++)
        if( strncmp( cmd, proto_msg_ack[i].signature, 8) == 0 )
            return proto_msg_ack[i].first;
    return 0;
}


int read_msg( proto_ctxt_t *pc, const uint8_t *data, int data_length)
{
    proto_elem_t *pe = pc->first_pe;
    msg_elem_t *first_elem = 0, *me;
    int p = 0;
    assert( pc->first_me == 0 );
    pc->length = data_length;
    
    for( ; pe; pe = pe->next)
    {
        if( first_elem == 0 )
        {
            me = (msg_elem_t *)proto_malloc( sizeof( msg_elem_t ) );
            first_elem = me;
        }
        else
        {
            me->next = (msg_elem_t *)proto_malloc( sizeof( msg_elem_t ) );
            me = me->next;
        }
        
        me->pe = pe;
        me->next = 0;
        me->nbr = pe->nbr;
#ifdef PICO_CLIENT
        me->rb = 0;
#endif
        
        switch( pe->type )
        {
        case T_INVALID:
            
            break;
        case T_INT16:
            if( p + sizeof( int16_t ) <= data_length )
            {
                memcpy( (void *)&(me->u.i16), data + p, sizeof( int16_t ));
                p += sizeof( int16_t );
            }
            else
                printf( "protocol error, input buffer length too short for int16\n" );
            break;
        case T_UINT16:
            if( p + sizeof( uint16_t ) <= data_length )
            {
                memcpy( (void *)&(me->u.u16), data + p, sizeof( uint16_t ));
                p += sizeof( uint16_t );
            }
            else
                printf( "protocol error, input buffer length too short for uint16\n" );
            break;
        case T_INT32:
            if( p + sizeof( int32_t ) <= data_length )
            {
                memcpy( (void *)&(me->u.i32), data + p, sizeof( int32_t ));
                p += sizeof( int32_t );
            }
            else
                printf( "protocol error, input buffer length too short for int32\n" );
            break;
        case T_UINT32:
            if( p + sizeof( uint32_t ) <= data_length )
            {
                memcpy( (void *)&(me->u.u32), data + p, sizeof( uint32_t ));
                p += sizeof( uint32_t );
            }
            else
                printf( "protocol error, input buffer length too short for uint32\n" );
            break;
        case T_DOUBLE:
            if( p + sizeof( double ) <= data_length )
            {
                memcpy( (void *)&(me->u.d), data + p, sizeof( double ));
                p += sizeof( double );
            }
            else
                printf( "protocol error, input buffer length too short for double\n" );
            break;
        case T_TIMESTAMP:  // 64 bit
            if( p + sizeof( int64_t ) <= data_length )
            {
                memcpy( (void *)&(me->u.ts), data + p, sizeof( int64_t ));
                p += sizeof( int64_t );
            }
            else
                printf( "protocol error, input buffer length too short for timestamp\n" );
            break;
        case T_STRING:     // variable length, 16 bit length info at begin
            {
                uint16_t len;
                
                if( p + sizeof( uint16_t ) <= data_length )
                {
                    memcpy( (void *)&len, data + p, sizeof( uint16_t ));
                    p += sizeof( uint16_t );
                }
                else
                    printf( "protocol error, input buffer length too short for string length (uint16)\n" );

                if( len > 0 )
                {
                    if( p + (int)len <= data_length )
                    {
                        me->u.s = (char *)proto_malloc( len+1 );
                        memcpy( me->u.s, data + p, len);
                        me->u.s[len] = 0;
                        me->slen = len;
                        p += len;
                    }
                    else
                    {
                        printf( "protocol error, input buffer length too short for string\n" );
                        
                        me->u.s = 0;
                        me->slen = 0;
                    }
                }
                else
                {
                    me->u.s = proto_strdup( "" );
                    me->slen = 0;
                }
            }
            break;
        case T_ARRAY_SAMPLE:     // variable length, 16 bit length info at begin
        case T_ARRAY_SAMPLE_FROM_RINGBUFFER:
            {
                uint16_t len;
                
                if( p + sizeof( uint16_t ) <= data_length )
                {
                    memcpy( (void *)&len, data + p, sizeof( uint16_t ));
                    p += sizeof( uint16_t );
                }
                else
                    printf( "protocol error, input buffer length too short for array of samples' size (uint16)\n" );
                
                if( len > 0 )
                {
                    if( p + (int)len * sizeof( sample_t ) <= data_length )
                    {
                        me->u.a = ( void *)proto_malloc( (int)len * sizeof( sample_t ) );
                        memcpy( me->u.a, data + p, (int)len * sizeof( sample_t ));
                        me->slen = len;
                        p += len * sizeof( sample_t );
                    }
                    else
                    {
                        printf( "protocol error, input buffer length too short for array of samples\n" );
                        me->u.a = 0;
                        me->slen = 0;
                    }
                }
                else
                {
                    me->u.a = 0;
                    me->slen = 0;
                }
            }
            break;
        case T_ARRAY_UINT16:     // variable length, 16 bit length info at begin
            {
                uint16_t len;
                
                if( p + sizeof( uint16_t ) <= data_length )
                {
                    memcpy( (void *)&len, data + p, sizeof( uint16_t ));
                    p += sizeof( uint16_t );
                }
                else
                    printf( "protocol error, input buffer length too short for array of uint16's size (uint16)\n" );

                if( len > 0 )
                {
                    if( p + (int)len * sizeof( uint16_t ) <= data_length )
                    {
                        me->u.a = ( void *)proto_malloc( (int)len * sizeof( uint16_t ) );
                        memcpy( me->u.a, data + p, (int)len * sizeof( uint16_t ));
                        me->slen = len;
                        p += len * sizeof( uint16_t );
                    }
                    else
                    {
                        printf( "protocol error, input buffer length too short for array of uint16s\n" );
                        me->u.a = 0;
                        me->slen = 0;
                    }
                }
                else
                {
                    me->u.a = 0;
                    me->slen = 0;
                }
            }
            break;
        case T_ARRAY_UINT32:     // variable length, 16 bit length info at begin
            {
                uint16_t len;
                
                if( p + sizeof( uint16_t ) <= data_length )
                {
                    memcpy( (void *)&len, data + p, sizeof( uint16_t ));
                    p += sizeof( uint16_t );
                }
                else
                    printf( "protocol error, input buffer length too short for array of uint32's size (uint16)\n" );

                if( len > 0 )
                {
                    if( p + (int)len * sizeof( uint32_t ) <= data_length )
                    {
                        me->u.a = ( void *)proto_malloc( (int)len * sizeof( uint32_t ) );
                        memcpy( me->u.a, data + p, (int)len * sizeof( uint32_t ));
                        me->slen = len;
                        p += len * sizeof( uint32_t );
                    }
                    else
                    {
                        printf( "protocol error, input buffer length too short for array of uint32s\n" );
                        me->u.a = 0;
                        me->slen = 0;
                    }
                }
                else
                {
                    me->u.a = 0;
                    me->slen = 0;
                }
            }
            break;
        }
    }
    
    pc->first_me = first_elem;
    return p;
}


int16_t get_elem_value_i16( proto_ctxt_t *pc, int nbr)
{
    msg_elem_t *me = pc->first_me;
    for( ; me; me = me->next)
    {
        if( me->nbr == nbr )
        {
            if( me->pe->type == T_INT16 )
            {
                return me->u.i16;
            }
            else
                printf("protocol error, wrong type at pos %d\n", nbr);
        }
    }

    printf("protocol error, pos %d not found\n", nbr);
    return 0;
}


uint16_t get_elem_value_u16( proto_ctxt_t *pc, int nbr)
{
    msg_elem_t *me = pc->first_me;
    for( ; me; me = me->next)
    {
        if( me->nbr == nbr )
        {
            if( me->pe->type == T_UINT16 )
            {
                return me->u.u16;
            }
            else
                printf("protocol error, wrong type at pos %d\n", nbr);
        }
    }

    printf("protocol error, pos %d not found\n", nbr);
    return 0;
}


int32_t get_elem_value_i32( proto_ctxt_t *pc, int nbr)
{
    msg_elem_t *me = pc->first_me;
    for( ; me; me = me->next)
    {
        if( me->nbr == nbr )
        {
            if( me->pe->type == T_INT32 )
            {
                return me->u.i32;
            }
            else
                printf("protocol error, wrong type at pos %d\n", nbr);
        }
    }

    printf("protocol error, pos %d not found\n", nbr);
    return 0;
}


uint32_t get_elem_value_u32( proto_ctxt_t *pc, int nbr)
{
    msg_elem_t *me = pc->first_me;
    for( ; me; me = me->next)
    {
        if( me->nbr == nbr )
        {
            if( me->pe->type == T_UINT32 )
            {
                return me->u.u32;
            }
            else
                printf("protocol error, wrong type at pos %d\n", nbr);
        }
    }

    printf("protocol error, pos %d not found\n", nbr);
    return 0;
}


double get_elem_value_d( proto_ctxt_t *pc, int nbr)
{
    msg_elem_t *me = pc->first_me;
    for( ; me; me = me->next)
    {
        if( me->nbr == nbr )
        {
            if( me->pe->type == T_DOUBLE )
            {
                return me->u.d;
            }
            else
                printf("protocol error, wrong type at pos %d\n", nbr);
        }
    }

    printf("protocol error, pos %d not found\n", nbr);
    return 0;
}


int64_t get_elem_value_ts( proto_ctxt_t *pc, int nbr)
{
    msg_elem_t *me = pc->first_me;
    for( ; me; me = me->next)
    {
        if( me->nbr == nbr )
        {
            if( me->pe->type == T_TIMESTAMP )
            {
                return me->u.ts;
            }
            else
                printf("protocol error, wrong type at pos %d\n", nbr);
        }
    }

    printf("protocol error, pos %d not found\n", nbr);
    return 0;
}


const char *get_elem_value_s( proto_ctxt_t *pc, int nbr)
{
    msg_elem_t *me = pc->first_me;
    for( ; me; me = me->next)
    {
        if( me->nbr == nbr )
        {
            if( me->pe->type == T_STRING )
            {
                return me->u.s;
            }
            else
                printf("protocol error, wrong type at pos %d\n", nbr);
        }
    }

    printf("protocol error, pos %d not found\n", nbr);
    return 0;
}


// array of samples
const sample_t *get_elem_array_samples( proto_ctxt_t *pc, int nbr, int *size)
{
    msg_elem_t *me = pc->first_me;
    *size = 0;
    
    for( ; me; me = me->next)
    {
        if( me->nbr == nbr )
        {
            if( me->pe->type == T_ARRAY_SAMPLE || me->pe->type == T_ARRAY_SAMPLE_FROM_RINGBUFFER )
            {
                *size = me->slen;
                return me->u.a;  // beware, can be 0 if me->slen = 0
            }
            else
                printf("protocol error, wrong type at pos %d\n", nbr);
        }
    }

    printf("protocol error, pos %d not found\n", nbr);
    return 0;
}

// array of uint16
const uint16_t *get_elem_array_u16( proto_ctxt_t *pc, int nbr, int *size)
{
    msg_elem_t *me = pc->first_me;
    *size = 0;
    
    for( ; me; me = me->next)
    {
        if( me->nbr == nbr )
        {
            if( me->pe->type == T_ARRAY_UINT16 )
            {
                *size = me->slen;
                return me->u.a;  // beware, can be 0 if me->slen = 0
            }
            else
                printf("protocol error, wrong type at pos %d\n", nbr);
        }
    }

    printf("protocol error, pos %d not found\n", nbr);
    return 0;
}

// array of uint32
const uint32_t *get_elem_array_u32( proto_ctxt_t *pc, int nbr, int *size)
{
    msg_elem_t *me = pc->first_me;
    *size = 0;
    
    for( ; me; me = me->next)
    {
        if( me->nbr == nbr )
        {
            if( me->pe->type == T_ARRAY_UINT32 )
            {
                *size = me->slen;
                return me->u.a;  // beware, can be 0 if me->slen = 0
            }
            else
                printf("protocol error, wrong type at pos %d\n", nbr);
        }
    }

    printf("protocol error, pos %d not found\n", nbr);
    return 0;
}


static void free_all_elem( msg_elem_t *me )
{
    while( me )
    {
        msg_elem_t *sav_next = me->next;

        if( me->pe->type == T_STRING )
            proto_free( me->u.s );
        else if( me->pe->type == T_ARRAY_SAMPLE || me->pe->type == T_ARRAY_UINT16 || me->pe->type == T_ARRAY_UINT32 )
        {
            if( me->slen > 0 )
                proto_free( me->u.a );
        }
        
        proto_free( me );
        me = sav_next;
    }
}


void proto_free_ctxt( proto_ctxt_t *pc )
{
    if( pc->first_me != 0 )
        free_all_elem( pc->first_me );
    pc->first_me = 0;
    pc->me = 0;
    if( pc->send_buffer != 0 )
        proto_free( pc->send_buffer );
    pc->send_buffer = 0;
}


static uint8_t *build_msg_by_proto( proto_elem_t *first_pe, msg_elem_t *first_me, int len)
{
    proto_elem_t *pe = first_pe;
    uint8_t *data = (uint8_t *)proto_malloc( len );
    if( !data )
        return 0;
    
    int p = 0;
    msg_elem_t *me = first_me;
    
    for( ; pe && me; pe = pe->next, me = me->next)
    {
        switch( pe->type )
        {
        case T_INVALID:
            
            break;
        case T_INT16:
            if( p + sizeof( int16_t ) <= len )
            {
                memcpy( data + p, (void *)&(me->u.i16), sizeof( int16_t ));
                p += sizeof( int16_t );
            }
            else
                printf("protocol error, output buffer too small for int16\n");
            break;
        case T_UINT16:
            if( p + sizeof( uint16_t ) <= len )
            {
                memcpy( data + p, (void *)&(me->u.u16), sizeof( uint16_t ));
                p += sizeof( uint16_t );
            }
            else
                printf("protocol error, output buffer too small for uint16\n");
            break;
        case T_INT32:
            if( p + sizeof( int32_t ) <= len )
            {
                memcpy( data + p, (void *)&(me->u.i32), sizeof( int32_t ));
                p += sizeof( int32_t );
            }
            else
                printf("protocol error, output buffer too small for int32\n");
            break;
        case T_UINT32:
            if( p + sizeof( uint32_t ) <= len )
            {
                memcpy( data + p, (void *)&(me->u.u32), sizeof( uint32_t ));
                p += sizeof( uint32_t );
            }
            else
                printf("protocol error, output buffer too small for uint32\n");
            break;
        case T_DOUBLE:
            if( p + sizeof( double ) <= len )
            {
                memcpy( data + p, (void *)&(me->u.d), sizeof( double ));
                p += sizeof( double );
            }
            else
                printf("protocol error, output buffer too small for double\n");
            break;
        case T_TIMESTAMP:  // 64 bit
            if( p + sizeof( int64_t ) <= len )
            {
                memcpy( data + p,(void *)&(me->u.ts), sizeof( int64_t ));
                p += sizeof( int64_t );
            }
            else
                printf("protocol error, output buffer too small for timestamp\n");
            break;
        case T_STRING:     // variable length, 16 bit length info at begin
            {
                if( p + sizeof( uint16_t ) + me->slen <= len )
                {
                    memcpy( data + p, (void *)&(me->slen), sizeof( uint16_t ));
                    p += sizeof( uint16_t );

                    if( me->slen > 0 )
                    {
                        memcpy( data + p, me->u.s, me->slen);
                        p += me->slen;
                    }
                }
                else
                    printf("protocol error, output buffer too small for string\n");
            }
            break;
        case T_ARRAY_SAMPLE:  // variable length, 16 bit length info at begin
            {
                if( p + sizeof( uint16_t ) + me->slen * sizeof(sample_t) <= len )
                {
                    memcpy( data + p, (void *)&(me->slen), sizeof( uint16_t ));
                    p += sizeof( uint16_t );
                    
                    if( me->slen > 0 )
                    {
                        memcpy( data + p, me->u.a, me->slen * sizeof(sample_t));
                        p += me->slen * sizeof(sample_t);
                    }
                }
                else
                    printf("protocol error, output buffer too small for array of samples\n");
            }
            break;
        case T_ARRAY_SAMPLE_FROM_RINGBUFFER:  // variable length, 16 bit length info at begin
#ifdef PICO_CLIENT
            {
                if( p + sizeof( uint16_t ) + me->slen * sizeof(sample_t) <= len )
                {
                    assert( me->rb );
                    
                    memcpy( data + p, (void *)&(me->slen), sizeof( uint16_t ));
                    p += sizeof( uint16_t );
                    
                    if( me->slen > 0 )
                    {
                        for( int i = 0; i < me->slen; i++)  // copy from ringbuffer directly to output buffer to save space
                        {
                            memcpy( data + p, consumer_get_next( me->rb, me->slen), sizeof(sample_t));
                            p += sizeof(sample_t);
                        }
                    }
                }
                else
                    printf("protocol error, output buffer too small for array of samples\n");
            }
#endif
            break;
        case T_ARRAY_UINT16:  // variable length, 16 bit length info at begin
            {
                if( p + sizeof( uint16_t ) + me->slen * sizeof(uint16_t) <= len )
                {
                    memcpy( data + p, (void *)&(me->slen), sizeof( uint16_t ));
                    p += sizeof( uint16_t );

                    if( me->slen > 0 )
                    {
                        memcpy( data + p, me->u.a, me->slen * sizeof( uint16_t ));
                        p += me->slen * sizeof( uint16_t );
                    }
                }
                else
                    printf("protocol error, output buffer too small for array of uint16\n");
            }
            break;
        case T_ARRAY_UINT32:  // variable length, 16 bit length info at begin
            {
                if( p + sizeof( uint16_t ) + me->slen * sizeof(uint32_t) <= len )
                {
                    memcpy( data + p, (void *)&(me->slen), sizeof( uint16_t ));
                    p += sizeof( uint16_t );

                    if( me->slen > 0 )
                    {
                        memcpy( data + p, me->u.a, me->slen * sizeof(uint32_t));
                        p += me->slen * sizeof(uint32_t);
                    }
                }
                else
                    printf("protocol error, output buffer too small for array of uint32\n");
            }
            break;
        }
    }

    if( !(pe==0 && me==0) )
    {
        printf("protocol error, mismatched number of elements\n");
        return 0;
    }
    
    return data;
}


void build_send_buffer( proto_ctxt_t *pc )
{
    assert( pc->first_pe );
    
    if( pc->send_buffer )
    {
        proto_free( pc->send_buffer );
        pc->send_buffer = 0;
    }
    
    if( pc->first_me && pc->length > 0 )
        pc->send_buffer = build_msg_by_proto( pc->first_pe, pc->first_me, pc->length);
}


void append_send_buffer( proto_ctxt_t *pc1, proto_ctxt_t *pc2)
{
    assert( pc1->first_pe );
    assert( pc1->first_me );
    assert( pc2->first_pe );
    if( !pc2->first_me )
    {
        printf( "pc2->first_me == 0\n" );
        return;
    }
    
    assert( pc1->length > 0 );
    if( pc2->length == 0 )
    {
        printf( "pc2->length == 0\n" );
        return;
    }

    if( !pc1->send_buffer )
    {
        printf( "pc1->send_buffer == 0\n" );
        return;
    }

    if( !pc2->send_buffer )
    {
        printf( "pc2->send_buffer == 0\n" );
        return;
    }
    
    pc1->send_buffer = proto_realloc( pc1->send_buffer, pc1->length + pc2->length);
    if( pc1->send_buffer == 0 )
    {
        printf( "append_send_buffer(): Out of memory\n" );
        return;
    }
    memcpy( pc1->send_buffer + pc1->length, pc2->send_buffer, pc2->length);
    pc1->length += pc2->length;
}


static msg_elem_t *new_msg_elem( proto_elem_t *pe, types_t type, const void *data, int *length, int arr_size)
{
    assert( pe );
    if( pe->type != type )
    {
        printf("protocol error, wrong type\n");
        return 0;
    }
    
    
    msg_elem_t *me = (msg_elem_t *)proto_malloc( sizeof( msg_elem_t ) );
    if( me == 0 )
    {
        printf( "\n\n\nOut of memory\n\n\n" );
        return 0;
    }
    
    me->pe = pe;
    me->nbr = pe->nbr;
    me->next = 0;
    
    switch( type )
    {
    case T_INVALID:
        
        break;
    case T_INT16:
        me->u.i16 = *((int16_t *)data);
        *length += sizeof( int16_t );
        break;
    case T_UINT16:
        me->u.u16 = *((uint16_t *)data);
        *length += sizeof( uint16_t );
        break;
    case T_INT32:
        me->u.i32 = *((int32_t *)data);
        *length += sizeof( int32_t );
        break;
    case T_UINT32:
        me->u.u32 = *((uint32_t *)data);
        *length += sizeof( uint32_t );
        break;
    case T_DOUBLE:
        me->u.d = *((double *)data);
        *length += sizeof( double );
        break;
    case T_TIMESTAMP:  // 64 bit
        me->u.ts = *((int64_t *)data);
        *length += sizeof( int64_t );
        break;
    case T_STRING:       // variable length, 16 bit length info at begin
        *length += sizeof( uint16_t );
        me->u.s = strdup( (const char *)data );
        if( me->u.s == 0 )
        {
            printf( "\n\n\nOut of memory\nT_STRING\n\n\n" );
            return 0;
        }
        me->slen = strlen( (const char *)data );
        *length += me->slen;
        break;
    case T_ARRAY_SAMPLE: // variable length, 16 bit length info at begin
        *length += sizeof( uint16_t );

        if( arr_size > 0 )
        {
            me->u.a = (void *)proto_malloc( arr_size * sizeof(sample_t) );
            if( me->u.a == 0 )
            {
                printf( "\n\n\nOut of memory\nT_ARRAY_SAMPLE\n\n\n" );
                return 0;
            }
            memcpy( me->u.a, data, arr_size * sizeof(sample_t));
            
            *length += arr_size * sizeof(sample_t);
        }
        else
            me->u.a = 0;
        me->slen = arr_size;
        break;
    case T_ARRAY_SAMPLE_FROM_RINGBUFFER: // variable length, 16 bit length info at begin
#ifdef PICO_CLIENT
        *length += sizeof( uint16_t );

        if( arr_size > 0 )
        {
            me->rb = (ringbuffer_t *)data;
            
            *length += arr_size * sizeof(sample_t);
        }
        else
            me->rb = 0;
        me->slen = arr_size;
#endif
        break;
    case T_ARRAY_UINT16: // variable length, 16 bit length info at begin
        *length += sizeof( uint16_t );

        if( arr_size > 0 )
        {
            me->u.a = (void *)proto_malloc( arr_size * sizeof(uint16_t) );
            if( me->u.a == 0 )
            {
                printf( "\n\n\nOut of memory\nT_ARRAY_UINT16\n\n\n" );
                return 0;
            }
            memcpy( me->u.a, data, arr_size * sizeof(uint16_t));
        
            *length += arr_size * sizeof(uint16_t);
        }
        else
            me->u.a = 0;
        me->slen = arr_size;
        break;
    case T_ARRAY_UINT32: // variable length, 16 bit length info at begin
        *length += sizeof( uint16_t );

        if( arr_size > 0 )
        {
            me->u.a = (void *)proto_malloc( arr_size * sizeof(uint32_t) );
            if( me->u.a == 0 )
            {
                printf( "\n\n\nOut of memory\nT_ARRAY_UINT32\n\n\n" );
                return 0;
            }
            memcpy( me->u.a, data, arr_size * sizeof(uint32_t));
        
            *length += arr_size * sizeof(uint32_t);
        }
        else
            me->u.a = 0;
        me->slen = arr_size;
        break;
    }
    
    return me;
}


void init_proto_ctxt( proto_ctxt_t *pc, const char cmd[4])
{
    pc->first_pe = find_proto( cmd );
    if( pc->first_pe == 0 )
    {
        printf( "proto " );
        for( int i = 0; i < 4; i++)
            printf( "%c", cmd[i]);
        printf( " not found\n" );
    }
    pc->pe = pc->first_pe;
    pc->first_me = pc->me = 0;
    pc->send_buffer = 0;
    pc->length = 0;
}


void init_proto_ctxt_ack( proto_ctxt_t *pc, const char cmd[8])
{
    pc->first_pe = find_proto_ack( cmd );
    if( pc->first_pe == 0 )
    {
        printf( "proto " );
        for( int i = 0; i < 8; i++)
            printf( "%c", cmd[i]);
        printf( " not found\n" );
    }
    pc->pe = pc->first_pe;
    pc->first_me = pc->me = 0;
    pc->send_buffer = 0;
    pc->length = 0;
}


static msg_elem_t *append_msg_elem( proto_ctxt_t *pc, types_t type, const void *data, int arr_size)
{
    msg_elem_t *new_me = new_msg_elem( pc->pe, type, data, &(pc->length), arr_size);
    if( new_me == 0 )
        return 0;
    
    pc->pe = pc->pe->next;
    if( pc->first_me == 0 )
    {
        pc->first_me = pc->me = new_me;
    }
    else
    {
        pc->me->next = new_me;
        pc->me = pc->me->next;
    }
    return new_me;
}


msg_elem_t *append_msg_elem_i16( proto_ctxt_t *pc, int16_t i16)
{
    return append_msg_elem( pc, T_INT16, (void *)&i16, 0);
}

void append_msg_elem_u16( proto_ctxt_t *pc, uint16_t u16)
{
    append_msg_elem( pc, T_UINT16, (void *)&u16, 0);
}

msg_elem_t *append_msg_elem_i32( proto_ctxt_t *pc, int32_t i32)
{
    return append_msg_elem( pc, T_INT32, (void *)&i32, 0);
}

void append_msg_elem_u32( proto_ctxt_t *pc, uint32_t u32)
{
    append_msg_elem( pc, T_UINT32, (void *)&u32, 0);
}

msg_elem_t *append_msg_elem_double( proto_ctxt_t *pc, double d)
{
    return append_msg_elem( pc, T_DOUBLE, (void *)&d, 0);
}

void append_msg_elem_ts( proto_ctxt_t *pc, int64_t ts)
{
    append_msg_elem( pc, T_TIMESTAMP, (void *)&ts, 0);
}

void append_msg_elem_s( proto_ctxt_t *pc, const char *s)
{
    if( strlen( s ) > 16 * 1024 )  // 16k ought to be enough for everyone
    {
        printf("protocol error, string too long\n");
        return;
    }
    append_msg_elem( pc, T_STRING, (void *)s, 0);
}

void append_msg_elem_array_samples( proto_ctxt_t *pc, const sample_t *samples, int size)
{
    if( size > (16 * 1024)/sizeof(sample_t) )  // 16k ought to be enough for everyone
    {
        printf("protocol error, array of samples' size too big\n");
        return;
    }
    append_msg_elem( pc, T_ARRAY_SAMPLE, (const void *)samples, size);
}

#ifdef PICO_CLIENT
void append_msg_elem_array_samples_from_ringbuffer( proto_ctxt_t *pc, const ringbuffer_t *samples, int size)
{
    if( size > (16 * 1024)/sizeof(sample_t) )  // 16k ought to be enough for everyone
    {
        printf("protocol error, array of samples' size too big\n");
        return;
    }
    append_msg_elem( pc, T_ARRAY_SAMPLE_FROM_RINGBUFFER, (const void *)samples, size);
}
#endif

void append_msg_elem_array_u16( proto_ctxt_t *pc, const uint16_t *a, int size)
{
    if( size > (16 * 1024)/sizeof(uint16_t) )  // 16k ought to be enough for everyone
    {
        printf("protocol error, array of uint16's size too big\n");
        return;
    }
    append_msg_elem( pc, T_ARRAY_UINT16, (const void *)a, size);
}

void append_msg_elem_array_u32( proto_ctxt_t *pc, const uint32_t *a, int size)
{
    if( size > (16 * 1024)/sizeof(uint32_t) )  // 16k ought to be enough for everyone
    {
        printf("protocol error, array of uint32's size too big\n");
        return;
    }
    append_msg_elem( pc, T_ARRAY_UINT32, (const void *)a, size);
}
