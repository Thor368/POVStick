/*
 * 	Doku, siehe http://www.mikrocontroller.net/articles/AVR_FAT32
 *  Neuste Version: http://www.mikrocontroller.net/svnbrowser/avr-fat32/
 *	Autor: Daniel R.
 */

#include <avr/interrupt.h>

#include "mmc_config.h"	// Hier werden alle noetigen Konfigurationen vorgenommen, umbedingt anschauen !
#include "file.h"
#include "fat.h"
#include "mmc.h"		// Hardware abhaengig
#include "uart.h"		// Hardware abhaengig, es kann auch eine eigene eingebunden werden !


// prototypen von funktionen in dieser datei
static void timer0_init(void);



// timer0 einstellungen, werte mit http://www.avrcalc.com/download.html berechnet!
// aus diesen 3 werten ergibt sich die tick zeit, hier 10ms. 
// 4 = prescaler 256, 3 = prescaler 64, 5 = prescaler 1024, 2 = prescaler 8. wenn prescaler 0 = prescaler dann stoppt der timer
#if(F_CPU == 4000000)			// error 0.16%
	#define TOP_OCR 0x9B 155
	#define START_TCNT 0x64 100
	#define PRESCALER 0x04 256
#endif

#if(F_CPU == 8000000)			// error 0,16%
	#define TOP_OCR 0x4D
	#define START_TCNT 0xB2
	#define PRESCALER 0x05
#endif

#if(F_CPU == 10000000)			// error 0.351%
	#define TOP_OCR 0x61
	#define START_TCNT 0x9E
	#define PRESCALER 0x05
#endif

#if(F_CPU == 12000000)			// error 0.16%
	#define TOP_OCR 0x74
	#define START_TCNT 0x8B
	#define PRESCALER 0x05
#endif

#if(F_CPU == 16000000)			// error 0,16%
	#define TOP_OCR 0x9B
	#define START_TCNT 0x64
	#define PRESCALER 0x05
#endif

#if(F_CPU == 20000000)			// error 0.16%
	#define TOP_OCR 0x4D
	#define START_TCNT 0xB2
	#define PRESCALER 0x04
#endif



// timer0 variable
volatile uint8_t 	TimingDelay;	// fuer mmc.c



// *****************************************************************************************************************
ISR (TIMER0_COMPA_vect)
{
	TimingDelay = (TimingDelay==0) ? 0 : TimingDelay-1;
}



// *****************************************************************************************************************
static void timer0_init(){

	TimingDelay = 0;		// initialisierung der zaehl variable	

	TCCR0A = 1<<WGM01; 		// timer0 im ctc mode
	TIMSK0 = 1<<OCIE0A;		// compare interrupt an

	TCNT0 = START_TCNT;		// ab wo hochgezaehlt wird,
	OCR0A = TOP_OCR;		// maximum bis wo gezaehlt wird bevor compare match	

	TCCR0B = PRESCALER;		// wenn prescaler gesetzt wird, lauft timer los
	sei();					// interrupts anschalten, wegen compare match
}



// *****************************************************************************************************************
void main(void){

	// lfn oder sfn
	#if (MMC_LFN_SUPPORT == TRUE)
		uint8_t file_name [] = "test_file.txt";
	#else 
		uint8_t file_name [] = "test.txt";
	#endif
	
	// string zum auf die karte schreiben.
	#if (MMC_WRITE == TRUE)
		uint8_t str [] = "Hallo Datei!";
	#endif 

	uint32_t seek;

	// timer0 config  **************************************************
	// initialisierung, auf jeden fall vor mmc_init(), 
	// denn da wird der timer benoetigt!
	timer0_init();

	// uart config *****************************************************
	uinit();

	uputs((uint8_t*)"\nBoot");

	// sd/mmc config  **************************************************
	if( FALSE == mmc_init() ){
		return;
	} 

	uputs((uint8_t*)"...");
			
	// fat config ******************************************************
	if( FALSE == fat_loadFatData() ){
		return;
	}

	// wenn auf dem terminal "Boot...OK" zu lesen ist, war initialisierung erfolgreich!
	uputs((uint8_t*)"OK\n");

	#if (MMC_WRITE ==TRUE)		// create and append only if write is TRUE
		// ****************************************************
		// if file exists, it will be opened and then append to it.
		if( MMC_FILE_OPENED == ffopen(file_name,'r') ){			
			ffseek(file.length);
		   	ffwrites(str);				
			ffwrite(0x0D);		// new line in file
		   	ffwrite(0x0A);
			ffclose();
		}
		
		// ***************************************************
		// if the file does not exist, it will be created and written to it.
		if(MMC_FILE_CREATED == ffopen(file_name,'c') ){
			ffwrites(str);				
			ffwrite(0x0D);		// new line in file
		   	ffwrite(0x0A);
			ffclose();
		} 	
	#endif 

	// ***************************************************
	// read file complete and print via uart!
	if( MMC_FILE_OPENED == ffopen(file_name,'r') ){			
		seek = file.length;
		do{
			uputc( ffread() );						
		}while(--seek);
		ffclose();
	}
	
	// retrun from main.c
	return;

}


#if (MMC_LS == TRUE)
// *****************************************************************************************************************
int8_t *ltostr(int32_t num, int8_t *string, uint16_t max_chars, uint8_t base)
{
      int8_t remainder;
      int sign = 0;   /* number of digits occupied by the sign. */

      if (base < 2 || base > 36)
            return FALSE;

      if (num < 0)
      {
            sign = 1;
            num = -num;
      }

      string[--max_chars] = '\0';

      for (max_chars--; max_chars > sign && num!=0; max_chars --)
      {
            remainder = (int8_t) (num % base);
            if ( remainder <= 9 )
                  string[max_chars] = remainder + '0';
            else  string[max_chars] = remainder - 10 + 'A';
            num /= base;
      }

      if (sign)
            string[--max_chars] = '-';

      if ( max_chars > 0 )
            memset(string, ' ', max_chars+1);

      return string + max_chars;
}
#endif
