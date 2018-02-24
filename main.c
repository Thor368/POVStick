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
#include <stdbool.h>
#include <stdio.h>

#include "configs.h"
#include "mmc_config.h"
#include "file.h"
#include "fat.h"
#include "mmc.h"

#include "FastSPI.h"

#define key_mod		(!(PINA & (1 << PA4)))
#define key_ok		(!(PINA & (1 << PA3)))

#define LED_write	FastSPI_write((uint8_t *) data_array, LED_count*3)


typedef struct
{
	uint8_t G;
	uint8_t R;
	uint8_t B;
} color_t;

enum estate {st_wait_start, st_menu_file, st_menu_brightness, st_menu_speed, st_menu_back}
	state = st_wait_start;

enum color {red, green, blue, yellow, magenta, cyan, white, black};

color_t data_array[LED_count];
uint32_t line_count = 0;  // 
uint8_t brightness = 1;  // brightness as 1 over the power of two = 0:100% 2:50% 3:25% 4:12.5%
uint8_t line_speed = 0;  // additional delay per line in ms
uint8_t file_index = 1;  // index of file to be played
bool menu_active = false;

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

void LED_sub_color(enum color c)
{
	for (uint16_t i = 0; i < LED_count; i++)
	{
		if ((c == black) || (c == green) || (c == blue) || (c == cyan))
			data_array[i].R = 0;
			
		if ((c == black) || (c == red) || (c == blue) || (c == magenta))
			data_array[i].G = 0;
			
		if ((c == black) || (c == green) || (c == red) || (c == yellow))
			data_array[i].B = 0;
	}
}

void LED_seggraph(uint8_t segments)
{
	if (segments > 30)
		return;
	
	LED_sub_color(black);
	
	uint16_t i = 0;
	for (uint8_t x = 0; x < segments; x++)
	{
		for (uint8_t y = 0; y < 5; y++)
		{
			data_array[i].R = 10;
			data_array[i].G = 10;
			data_array[i].B = 10;
			i++;
		}
		
		for (uint8_t y = 0; y < 3; y++)
		{
			data_array[i].R = 0;
			data_array[i].G = 0;
			data_array[i].B = 0;
			i++;
		}
		
		if (((x+1)%4) == 0)
		{
			for (uint8_t y = 0; y < 7; y++)
			{
				data_array[i].R = 0;
				data_array[i].G = 0;
				data_array[i].B = 0;
				i++;
			}
		}
	}
}

void LED_menu_file(bool active)
{
	LED_seggraph(file_index);
	
	if (active)
		LED_sub_color(white);
	else
		LED_sub_color(yellow);
	
	LED_write;
}

void LED_menu_brightness(bool active)
{
	LED_seggraph(brightness);
	
	if (active)
		LED_sub_color(white);
	else
		LED_sub_color(blue);
	
	LED_write;
}

void LED_menu_speed(bool active)
{
	LED_seggraph(line_speed + 1);
	
	if (active)
		LED_sub_color(white);
	else
		LED_sub_color(green);
	
	LED_write;
}

void LED_menu_back()
{
	LED_seggraph(1);
	
	LED_sub_color(red);
	LED_write;
}

bool file_select()
{
	char buffer[20], comp[20];
	uint32_t start_offset, width;
	
	ffclose();
	
	sprintf(buffer, "%02d_pixelstick.bmp", file_index);
//	strcpy(buffer, "1_pixelstick.bmp");
	
	if (ffileExsists((uint8_t*) buffer) == TRUE)  // check if requested file is present
	{
		ffopen((uint8_t *) buffer, 'r');  // open it
		read_string(2, buffer);
		strcpy(comp, "BM");
		if (!strcmp(buffer, comp))  // check if it is a BMP
		{
			ffseek(0x0A);
			read_block(4, (char *) &start_offset);  // get start of image data
			
			ffseek(0x12);
			read_block(4, (char *) &width);
			if (width == LED_count)  // check if BMP is correctly sized
			{
				ffseek(0x16);
				read_block(4, (char *) &line_count);  // get height or line count
				
				ffseek(start_offset);
				return true;
			}
		}
	}
	
	return false;
}

void delay_ms(uint16_t t)
{
	for (;t > 0; t--)
		_delay_ms(1);
}

void file_display()
{
	for (uint32_t i = 0; i < line_count; i++)
	{
		for (uint16_t j = 0; j < LED_count; j++)
		{
			data_array[j].B = ffread() >> brightness;
			data_array[j].G = ffread() >> brightness;
			data_array[j].R = ffread() >> brightness;
		}
					
		FastSPI_write((uint8_t *) data_array, LED_count*3);
		delay_ms(line_speed);
	}
}

void InitSYS()
{
	DDRD |= (1 << PD5);
	PORTA |= (1 << PA3) | (1 << PA4);
	
	LED_sub_color(black);
	LED_write;
	
	if (mmc_init() == FALSE)
		while (1);
	
	if (fat_loadFatData() == FALSE)
		while (1);
}

int main(void)
{
	InitSYS();
	
    while (1)
	{
		switch (state)
		{
			case st_wait_start:
				if key_mod
				{
					state = st_menu_file;
					menu_active = false;
					LED_menu_file(false);
					_delay_ms(200);
				}
				else if key_ok
				{
					_delay_ms(1000);
					if (file_select())
						file_display();
					LED_sub_color(black);
					LED_write;
				}
			break;
			
			case st_menu_file:
				if (menu_active)
				{
					if (key_ok)
					{
						menu_active = false;
						LED_menu_file(false);
						_delay_ms(200);
					}
					else if (key_mod)
					{
						if (file_index < 30)
							file_index++;
						else
							file_index = 1;
					
						LED_menu_file(true);
						_delay_ms(200);
					}
				}
				else
				{
					if (key_ok)
					{
						menu_active = true;
						LED_menu_file(true);
						_delay_ms(200);
					}
					else if (key_mod)
					{
						state = st_menu_brightness;
						LED_menu_brightness(false);
						_delay_ms(200);
					}
				}
			break;
			
			case st_menu_brightness:
				if (menu_active)
				{
					if (key_ok)
					{
						menu_active = false;
						LED_menu_brightness(false);
						_delay_ms(200);
					}
					else if (key_mod)
					{
						if (brightness < 5)
							brightness++;
						else
							brightness = 1;
					
						LED_menu_brightness(true);
						_delay_ms(200);
					}
				}
				else
				{
					if (key_ok)
					{
						menu_active = true;
						LED_menu_brightness(true);
						_delay_ms(200);
					}
					else if (key_mod)
					{
						state = st_menu_speed;
						LED_menu_speed(false);
						_delay_ms(200);
					}
				}
			break;
			
			case st_menu_speed:
				if (menu_active)
				{
					if (key_ok)
					{
						menu_active = false;
						LED_menu_speed(false);
						_delay_ms(200);
					}
					else if (key_mod)
					{
						if (line_speed < 30)
							line_speed++;
						else
							line_speed = 0;
					
						LED_menu_speed(true);
						_delay_ms(200);
					}
				}
				else
				{
					if (key_ok)
					{
						menu_active = true;
						LED_menu_speed(true);
						_delay_ms(200);
					}
					else if (key_mod)
					{
						state = st_menu_back;
						LED_menu_back();
						_delay_ms(200);
					}
				}
			break;
			
			case st_menu_back:
				if (key_ok)
				{
					state = st_wait_start;
					LED_sub_color(black);
					LED_write;
					_delay_ms(200);
				}
				else if (key_mod)
				{
					state = st_menu_file;
					LED_menu_file(false);
					_delay_ms(200);
				}
			break;
			
			default:
				state = st_wait_start;
				break;
		}
	}
}
