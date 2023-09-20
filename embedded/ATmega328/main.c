#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <avr/io.h>
#include <util/delay.h>

#include <avr/interrupt.h>

// turn transmit hex on for debugging
// #define TRANSMIT_HEX

// https://www.mikrocontroller.net/articles/High-Speed_capture_mit_ATmega_Timer

#define LED PD7

static void uart_puts( const char * s );
static char uart_getc(void);

static void do_measure();

int main()
{
    // Set port D
    DDRD = _BV(LED);

    // configure UART
    // F_CPU has to be set for this to work
    // see https://www.nongnu.org/avr-libc/user-manual/group__util__setbaud.html
//#define BAUD 115200
// 156250 works for 10 Mhz clock
#define BAUD 156250
    
#include <util/setbaud.h>
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;
#if USE_2X
    UCSR0A |= (1 << U2X0);
#else
    UCSR0A &= ~(1 << U2X0);
#endif
    
    UCSR0C = 3<<UCSZ00;  // 8N1
    
    // UCSR0C |= 1 << USBS0; // 2 stop bits
    
	// UCSR0B = (1<<TXEN0); // enable transmitter
    UCSR0B |= (1 << RXEN0) | (1 << TXEN0); // enable transmitter & receiver 
    
    
    DDRB &= ~(1 << DDB1);         // Clear the PB1 pin
    // PB0 (PCINT0 pin) is now an input
    PORTB = 0;
    // PORTB |= (1 << PORTB1);        // turn On the Pull-up

    /*
    PCICR |= (1 << PCIE0);     // set PCIE0 to enable PCMSK0 scan
    PCMSK0 |= (1 << PCINT1);   // set PCINT1 to trigger an interrupt on state change

    sei();                     // turn on interrupts
    */

    do_measure();
}



/*
ISR (PCINT0_vect)
{
    if( (PINB & (1 << PINB1))  )
    {
        // LOW to HIGH pin change
        uart_puts("- L->H");
        PORTD ^= _BV(LED);
    }
    else
    {
        // HIGH to LOW pin change
    }
}
*/

// +-----------------------------------------------------------------------+ //
static char uart_getc(void)
{
    // wait for data
    while(!(UCSR0A & (1 << RXC0)))
        ;

    return UDR0;
}


static void uart_putc( char c )
{
    while( !( UCSR0A & (1<<UDRE0)) )  // wait for empty transmit buffer
        ;
    UDR0 = c;
}


static void uart_puts( const char * s )
{
    for( int i = 0; s[i] != 0; i++)
        uart_putc( s[i] );
}


static void uart_print( const char *s )
{
    uart_puts( s );
}


static void uart_println( const char *s )
{
    uart_puts( s );
    uart_putc( '\n' );
}



#define OVL_CNT 65536

static uint8_t ovl;  // counts overflows
static uint16_t sec_barrier_cnt;  // remember counter value from seconds barrier
// reset whenever 1PPS has a rising edge
// F_CPU must be 10Mhz, 1PPS connected to PINB1
// floor(10000000/ovl_val) = 152
#define RESET_OVL {if( ovl >= 150 && (PINB & (1 << PINB1)) ) { sec_barrier_cnt = TCNT1; ovl = 0;}}

static void uart_send_meas_putc( uint8_t c )
{
    while( !( UCSR0A & (1<<UDRE0)) )
        RESET_OVL;               // ; to make emacs happy
    UDR0 = c;
}

#if defined(TRANSMIT_HEX)
static const char hexvals[16] = {
    '0', '1', '2', '3',
    '4', '5', '6', '7',
    '8', '9', 'A', 'B',
    'C', 'D', 'E', 'F'
};

static void uart_send_meas_printbhex( uint8_t b )
{
    uart_send_meas_putc( hexvals[ b >> 4 ] );
    RESET_OVL;
    uart_send_meas_putc( hexvals[ b & 0xf ] );
    RESET_OVL;
}

static void uart_send_meas_printwhex( uint16_t w )
{
    uart_send_meas_printbhex( w >> 8 );
    RESET_OVL;
    uart_send_meas_printbhex( w & 0xff );
    RESET_OVL;
}

static void uart_send_meas_printtshex( uint32_t ts )  
{
    uart_send_meas_printbhex( (ts >> 24) & 0xff );
    RESET_OVL;
    uart_send_meas_printbhex( (ts >> 16) & 0xff );
    RESET_OVL;
    uart_send_meas_printbhex( (ts >> 8) & 0xff );
    RESET_OVL;
    uart_send_meas_printbhex( ts & 0xff );
    RESET_OVL;
}

#else

static void uart_send_meas_printts( uint32_t ts )  
{
    uint8_t *c = (uint8_t *)&ts;
    uint8_t chk = c[0] + c[1] + c[2] + c[3]; // overflow intentional
    RESET_OVL;
    uart_send_meas_putc( c[0] );
    uart_send_meas_putc( c[1] );
    uart_send_meas_putc( c[2] );
    uart_send_meas_putc( c[3] );
    uart_send_meas_putc( chk );
}
#endif

// counter for led blink
static uint8_t led_c = 0;

// sends 32 bit timestamp to PicoW
// 1 byte signature
// 4 byte timestamp (binary)
// 1 byte checksum (binary)
// 1 byte return char
// if TRANSMIT_HEX is on it is 1 + 8 + 1, no checksum in hex mode
static void uart_send_meas( char sig, uint32_t ts)
{
    uart_send_meas_putc( sig );    // signature '/' => rising edge, '\' => falling edge
    
#if defined(TRANSMIT_HEX)
    uart_send_meas_printtshex( ts );
#else
    uart_send_meas_printts( ts );
#endif
    
    uart_send_meas_putc( '\n' );

    if( led_c == 50 )
    {
        PORTD ^= _BV(LED);
        led_c = 0;
    }
    else
        led_c++;
    RESET_OVL;
}


static void loop()
{
    bool rise = true;
    TCCR1B = 0x41;      // rising edge, no prescaler
    TIFR1 = (1<<ICF1);      // clear ICF flag
    uint16_t cap = 0;
    uint8_t cap_ovl;
    int32_t timestamp;
    
    while( true )
    {
        if(  ovl >= 150 && (PINB & (1 << PINB1)))  // 1PPS low->high transition?
        {
            ovl = 0;
            sec_barrier_cnt = TCNT1;
        }
        
        register uint8_t tifr = TIFR1;
        if( (tifr & _BV(ICF1)) )  // check for capture
        {
            cap = ICR1;                           // take value of capture register
            TIFR1 = (1<<ICF1);                    // clear capture
            
            if( ovl > 0 )
            {
                cap_ovl = ovl;
                timestamp = (int32_t)cap + cap_ovl * OVL_CNT - (int32_t)sec_barrier_cnt;
            }
            else if( cap < sec_barrier_cnt )  // timestamps must never be negative
            {
                cap_ovl = ovl = 1;
                timestamp = (int32_t)cap + OVL_CNT - (int32_t)sec_barrier_cnt;
            }
            else
            {
                cap_ovl = 0;
                timestamp = (int32_t)cap - (int32_t)sec_barrier_cnt;
            }
            
            if( rise )
                uart_send_meas( '/', timestamp);    // send rising timestamp to PicoW
            else
                uart_send_meas( '\\', timestamp);   // send falling timestamp to PicoW
            
            rise = !rise;
            if( rise )
                TCCR1B = 0x41;      // rising edge, no prescaler
            else
                TCCR1B = 0x01;      // falling edge, no prescaler
        }
        
        if( (tifr & _BV(OCF1A)) )        // check for overflow
        {
            TIFR1 = _BV(OCF1A);
            
            ovl++;
        }
    }
}


static void do_measure()
{
    //  PORTD = 0xFF;           // turn on pull-up resistor
    cli(); // disable IRQs
    
    TCCR1A = 0;
    ovl = 0;
    
    // lines who start with '-' are printed by PicoW, not a timestamp
    uart_println( "-------------------------------------------------------" );
    uart_println( "- trying to sync" );
    
    // synchronize with 1PPS
    while( (PINB & (1 << PINB1)) )   // loop while 1
        ;
    while( !(PINB & (1 << PINB1)) )  // loop while 0
        ;
    sec_barrier_cnt = TCNT1;
    TIFR1 = _BV(OCF1A);     // clear overflow flag
    uart_println( "-" );
    uart_println( "- synced" );
    
    loop();
}
