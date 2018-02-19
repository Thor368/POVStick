/*
 * config.h
 *
 * Created: 14.11.2017 23:21:56
 *  Author: main
 */ 


#define LED_on    PORTA |= (1 << PA0)
#define LED_off   PORTA &= ~(1 << PA0)
#define LED_tog   PORTA ^= (1 << PA0)

#define LED_count  288
