/*
 * FastSPI.h
 *
 * Created: 25.02.2015 21:13:07
 *  Author: main
 */ 

#include <avr/io.h>

#ifndef FASTSPI_H_
#define FASTSPI_H_

extern void FastSPI_write(uint8_t data[], uint16_t count);

#endif /* FASTSPI_H_ */