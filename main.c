/*
 * POCStick.c
 *
 * Created: 11.11.2017 14:54:44
 * Author : main
 */ 

#define F_CPU 17734476

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdlib.h>
#include <string.h>

#include "configs.h"
#include "mmc_config.h"
#include "file.h"
#include "fat.h"
#include "mmc.h"

#include "FastSPI.h"

#define brightness		1  // power of two. 0:100% 1:50% 2:25% 3:12,5% ...


typedef struct
{
	uint8_t G;
	uint8_t R;
	uint8_t B;
} color_t;

color_t data_array[LED_count];

void read_block(uint16_t count, char* buf)
{
	for (uint16_t i=0; i < count; i++)
		*(buf++) = ffread();
}

void read_string(uint16_t count, char* buf)
{
	for (uint16_t i=0; i < count; i++)
		*(buf++) = ffread();
	*buf = 0;
}

int main(void)
{
	DDRA |= (1 << PA0);
	DDRD |= (1 << PD5);
	
	char buffer[20], comp[20];
	uint32_t start_offset = 0;
	uint32_t width = 0;
	
	for (uint16_t i = 0; i < LED_count; i++)
	{
		data_array[i].R = 0;
		data_array[i].G = 0;
		data_array[i].B = 0;
	}
	FastSPI_write((uint8_t *) data_array, LED_count*3);
	
	_delay_ms(1000);
	
	if (mmc_init() == FALSE)
		while(1);
	
	if (fat_loadFatData() == FALSE)
		while(1);

	if (ffileExsists((uint8_t*) "test.bmp") == TRUE)
	{
		ffopen((uint8_t *) "test.bmp", 'r');
		read_string(2, buffer);
		strcpy(comp, "BM");
		if (!strcmp(buffer, comp))
		{
			ffseek(0x0A);
			read_block(4, (char *) &start_offset);
			
			ffseek(0x12);
			read_block(4, (char *) &width);
			if (width == LED_count)
			{
				ffseek(0x16);
				read_block(4, (char *) &width);
				
				ffseek(start_offset);
				for (uint32_t i = 0; i < width; i++)
				{
					for (uint16_t j = 0; j < LED_count; j++)
					{
						data_array[j].B = ffread() >> brightness;
						data_array[j].G = ffread() >> brightness;
						data_array[j].R = ffread() >> brightness;
						if ((data_array[j].R == 0) && (data_array[j].G == 0) && (data_array[j].B == 0))
							LED_on;
					}
					
					FastSPI_write((uint8_t *) data_array, LED_count*3);
				}
			}
		}
	}
	
	for (uint16_t i = 0; i < LED_count; i++)
	{
		data_array[i].R = 0;
		data_array[i].G = 0;
		data_array[i].B = 0;
	}
	FastSPI_write((uint8_t *) data_array, LED_count*3);

    while (1);
}
