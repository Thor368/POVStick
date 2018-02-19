/*
 * 	Doku, siehe http://www.mikrocontroller.net/articles/AVR_FAT32
 *  Neuste Version: http://www.mikrocontroller.net/svnbrowser/avr-fat32/
 *	Autor: Daniel R.
 */

#include "configs.h"
#include "mmc_config.h"
#include "mmc.h"

// Definitions for MMC/SDC command 
#define CMD0	(0)			// GO_IDLE_STATE 
#define CMD1	(1)			// SEND_OP_COND (MMC) 
#define	ACMD41	(41)		// SEND_OP_COND (SDC)
#define CMD8	(8)			// SEND_IF_COND 
#define CMD9	(9)			// SEND_CSD 
#define CMD10	(10)		// SEND_CID 
#define CMD12	(12)		// STOP_TRANSMISSION 
#define ACMD13	(0x80+13)	// SD_STATUS (SDC) 
#define CMD16	(16)		// SET_BLOCKLEN 
#define CMD17	(17)		// READ_SINGLE_BLOCK 
#define CMD18	(18)		// READ_MULTIPLE_BLOCK 
#define CMD23	(23)		// SET_BLOCK_COUNT (MMC) 
#define	ACMD23	(0x80+23)	// SET_WR_BLK_ERASE_COUNT (SDC) 
#define CMD24	(24)		// WRITE_BLOCK 
#define CMD25	(25)		// WRITE_MULTIPLE_BLOCK 
#define CMD55	(55)		// APP_CMD 
#define CMD58	(58)		// READ_OCR 

// Card type flags (CardType) 
#define CT_MMC		0x01			// MMC ver 3 
#define CT_SD1		0x02			// SD ver 1 
#define CT_SD2		0x04			// SD ver 2 
#define CT_SDC		(CT_SD1|CT_SD2)	// SD 
#define CT_BLOCK	0x08			// Block addressing 

// **********************************************************************************************************************************
// funktionsprototypen von funktionen die nur in dieser datei benutzt werden !

static uint8_t 	mmc_enable(void);
static void 			mmc_disable(void); 
static uint8_t 	mmc_wait_ready (void);
static uint8_t 	mmc_send_cmd (	uint8_t cmd,	uint32_t arg);

// beginn -> hardware abhaengiger teil !
#define MMC_CS_LOW 		MMC_Write &= ~(1<<SPI_SS)		// Set pin B2 to 0
#define MMC_CS_HIGH		MMC_Write |= (1<<SPI_SS)		// Set pin B2 to 1

static void 			spi_init(void);
static void 			spi_maxSpeed(void);
static void 			spi_write_byte(uint8_t byte);
static uint8_t 	spi_read_byte(void);


// *****************************************************************************
static void spi_init(void){
 	
	// port configuration der mmc/sd/sdhc karte
	MMC_Direction_REG &=~(1<<SPI_MISO);         // miso auf input
	MMC_Direction_REG |= (1<<SPI_Clock);      	// clock auf output
	MMC_Direction_REG |= (1<<SPI_MOSI);         // mosi auf output
	MMC_Direction_REG |= (1<<SPI_SS);			// chip select auf output

	// hardware spi: bus clock = idle low, spi clock / 128 , spi master mode
	SPCR = (1<<SPE)|(1<<MSTR)|(1<<SPR0)|(1<<SPR1);

	MMC_Write |= (1<<SPI_SS);       	// chip selet auf high, karte anwaehlen
}



#if (MMC_MAX_SPEED==TRUE)
// *****************************************************************************
static void spi_maxSpeed(){
	
	//SPI Bus auf max Geschwindigkeit
	SPCR &= ~((1<<SPR0) | (1<<SPR1));
	SPSR |= (1<<SPI2X);
}
#endif

// *****************************************************************************
static void spi_write_byte(uint8_t byte){
	

	#if (MMC_SOFT_SPI==TRUE)
		uint8_t a;
	#endif

	// mmc/sd in hardware spi
	#if (MMC_SOFT_SPI==FALSE)
		SPDR = byte;    						//Sendet ein Byte
		loop_until_bit_is_set(SPSR,SPIF);

	// mmc/sd in software spi
	#else
		for (a=8; a>0; a--){					//das Byte wird Bitweise nacheinander Gesendet MSB First
			if (bit_is_set(byte,(a-1))>0){		//Ist Bit a in Byte gesetzt
				MMC_Write |= (1<<SPI_MOSI); 	//Set Output High
			}
			else{
				MMC_Write &= ~(1<<SPI_MOSI); 	//Set Output Low
			}
			MMC_Write |= (1<<SPI_Clock); 		//setzt Clock Impuls wieder auf (High)
			MMC_Write &= ~(1<<SPI_Clock);		//erzeugt ein Clock Impuls (LOW)
		}
		MMC_Write |= (1<<SPI_MOSI);				//setzt Output wieder auf High
	#endif
}


// *****************************************************************************
static uint8_t spi_read_byte(void){
	
	// mmc/sd in hardware spi
	#if (MMC_SOFT_SPI==FALSE)
	  SPDR = 0xff;
	  loop_until_bit_is_set(SPSR,SPIF);
	  return (SPDR);

	// mmc/sd in software spi
	#else
	    uint8_t Byte=0;
	    uint8_t a;
		for (a=8; a>0; a--){							//das Byte wird Bitweise nacheinander Empangen MSB First
			MMC_Write |=(1<<SPI_Clock);					//setzt Clock Impuls wieder auf (High)
			if (bit_is_set(MMC_Read,SPI_MISO) > 0){ 	//Lesen des Pegels von MMC_MISO
				Byte |= (1<<(a-1));
			}
			else{
				Byte &=~(1<<(a-1));
			}
			MMC_Write &=~(1<<SPI_Clock); 				//erzeugt ein Clock Impuls (Low)
		}
		return (Byte);
	#endif
}



// ende <- hardware abhaengiger teil !








// **********************************************************************************************************************************
uint8_t mmc_init (void){

	uint8_t cmd, ty, ocr[4];
	uint16_t n, j;

	spi_init();
	mmc_disable();

	for (n = 100; n; n--) spi_read_byte();    					// 80+ dummy clocks

	ty = 0;
	j=100;
	do {
		if (mmc_send_cmd(CMD0, 0) == 1) {      					// Enter Idle state
			j=0;

			if (mmc_send_cmd(CMD8, 0x1AA) == 1) {  				// SDv2?
				for (n = 0; n < 4; n++){
					ocr[n] = spi_read_byte();    				// Get trailing return value of R7 resp
				}
				if (ocr[2] == 0x01 && ocr[3] == 0xAA) {         // The card can work at vdd range of 2.7-3.6V
					while (1) {  						// Wait for leaving idle state (ACMD41 with HCS bit)
						mmc_send_cmd(CMD55, 0);
						if(!mmc_send_cmd(ACMD41, 1UL << 30))
							break;
					}

					while(1) {
						if (mmc_send_cmd(CMD58, 0) == 0x00) {    // Check CCS bit in the OCR
							for (n = 0; n < 4; n++){
								ocr[n] = spi_read_byte();
							}
							ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;  // SDv2
							break;
						}
					}
				}
			} else {        									// SDv1 or MMCv3
				if (mmc_send_cmd(ACMD41, 0) <= 1)   {
					ty = CT_SD1;
					cmd = ACMD41;  								// SDv1
				} else {
					ty = CT_MMC;
					cmd = CMD1;    								// MMCv3
				}
				while (mmc_send_cmd(cmd, 0));    // Wait for leaving idle state
			}
			if(ty != (CT_SD2 | CT_BLOCK)) {
				while(mmc_send_cmd(CMD16, 512) != 0);
			}
		} else { j--; }
	}while(j>0);

	fat.card_type = ty;
	mmc_disable();

	if( fat.card_type == 0 ){
		return FALSE;
	}
	#if (MMC_MAX_SPEED==TRUE)
		spi_maxSpeed();
	#endif

	return TRUE;
}

// **********************************************************************************************************************************
static uint8_t mmc_send_cmd (	uint8_t cmd,	uint32_t arg){
	
	uint8_t n, res;
	// Select the card and wait for ready 
	mmc_disable();
	if ( FALSE == mmc_enable() ){
		return 0xFF;
	}
	// Send command packet 
	spi_write_byte(0x40 | cmd);						// Start + Command index 
	spi_write_byte( (uint8_t)(arg >> 24) );	// Argument[31..24]
	spi_write_byte( (uint8_t)(arg >> 16) );	// Argument[23..16]
	spi_write_byte( (uint8_t)(arg >> 8) );	// Argument[15..8]
	spi_write_byte( (uint8_t)arg );			// Argument[7..0]
	n = 0x01;										// Dummy CRC + Stop 
	if (cmd == CMD0) n = 0x95;						// Valid CRC for CMD0(0) 
	if (cmd == CMD8) n = 0x87;						// Valid CRC for CMD8(0x1AA) 
	spi_write_byte(n);

	// Receive command response 
	if (cmd == CMD12) spi_read_byte();				// Skip a stuff byte when stop reading 
	n = 10;											// Wait for a valid response in timeout of 10 attempts 
	do
		res = spi_read_byte();
	while ( (res & 0x80) && --n );

	return res;										// Return with the response value 
}





// **********************************************************************************************************************************
static uint8_t mmc_enable(){
      
   MMC_CS_LOW;
   if( !mmc_wait_ready() ){
   	  mmc_disable();
	  return FALSE;
   }

   return TRUE;
}

// **********************************************************************************************************************************
static void mmc_disable(){

   MMC_CS_HIGH;   
   spi_read_byte();
}


#if (MMC_MULTI_BLOCK==TRUE && MMC_OVER_WRITE == FALSE)
// **********************************************************************************************************************************
// stopt multiblock lesen
// **********************************************************************************************************************************
uint8_t mmc_multi_block_stop_read (void){

	uint8_t cmd[] = {0x40+12,0x00,0x00,0x00,0x00,0xFF};	// CMD12 (stop_transmission), response R1b (kein fehler, dann 0)
	uint8_t response;

	response = mmc_write_command (cmd);		// r1 antwort auf cmd12

	response = mmc_read_byte();				// dummy byte nach cmd12

	mmc_disable();
	return response;
}


// **********************************************************************************************************************************
// stop multiblock schreiben
// **********************************************************************************************************************************
uint8_t mmc_multi_block_stop_write (void){

	uint8_t cmd[] = {0x40+13,0x00,0x00,0x00,0x00,0xFF};	// CMD13 (send_status), response R2
	uint8_t response;

	mmc_write_byte(0xFD);					// stop token

	mmc_wait_ready();

	response=mmc_write_command (cmd);		// cmd13, alles ok?

	mmc_wait_ready();

	mmc_disable();
	return response;
}


// **********************************************************************************************************************************
// starten von multi block read. ab sektor addr wird der reihe nach gelesen. also addr++ usw...
// **********************************************************************************************************************************
uint8_t mmc_multi_block_start_read (uint32_t int addr){

	uint8_t cmd[] = {0x40+18,0x00,0x00,0x00,0x00,0xFF};	// CMD18 (read_multiple_block), response R1
	uint8_t response;

	mmc_enable();

	// addressiertung bei mmc und sd (standart < 2.0) in bytes, also muss sektor auf byte adresse umgerechnet werden.
	// sd standart > 2.0, adressierung in sektoren, also 512 byte bloecke
	if(card_type==0) addr = addr << 9; //addr = addr * 512, nur wenn mmc/sd karte vorliegt

	cmd[1] = ((addr & 0xFF000000) >>24 );
	cmd[2] = ((addr & 0x00FF0000) >>16 );
	cmd[3] = ((addr & 0x0000FF00) >>8 );
	cmd[4] = (addr &  0x000000FF);

	mmc_wait_ready ();

	response=mmc_write_command (cmd);		// commando senden und response speichern

	while (mmc_read_byte() != 0xFE){		// warten auf start byte
		nop();
	};

	return response;
}


// **********************************************************************************************************************************
//multi block lesen von sektoren. bei aufruf wird immer ein sektor gelesen und immer der reihe nach
// **********************************************************************************************************************************
void mmc_multi_block_read_sector (uint8_t *Buffer){

	uint16_t a; 							// einfacher zähler fuer bytes eines sektors

	// mmc/sd in hardware spi, block lesen
	#if (MMC_SOFT_SPI==FALSE)
	   uint8_t tmp; 						// hilfs variable zur optimierung
	   a=512;
	   SPDR = 0xff;								// dummy byte
		do{										// 512er block lesen
			loop_until_bit_is_set(SPSR,SPIF);
			tmp=SPDR;
			SPDR = 0xff;						// dummy byte
			*Buffer=tmp;
			Buffer++;
		}while(--a);

	// mmc/sd/sdhc in software spi, block lesen
	#else
		a=512;
		do{
			*Buffer++ = mmc_read_byte();
		}while(--a);
	#endif

	mmc_read_byte();						// crc byte
	mmc_read_byte();						// crc byte

	while (mmc_read_byte() != 0xFE){		// warten auf start byte 0xFE, damit fängt jede datenuebertragung an...
		nop();
		}
}


// **********************************************************************************************************************************
// starten von multi block write. ab sektor addr wird der reihe nach geschrieben. also addr++ usw...
// **********************************************************************************************************************************
uint8_t mmc_multi_block_start_write (uint32_t int addr){

	uint8_t cmd[] = {0x40+25,0x00,0x00,0x00,0x00,0xFF};	// CMD25 (write_multiple_block),response R1
	uint8_t response;

	mmc_enable();

	// addressiertung bei mmc und sd (standart < 2.0) in bytes, also muss sektor auf byte adresse umgerechnet werden.
	// sd standart > 2.0, adressierung in sektoren, also 512 byte bloecke
	if(card_type==0) addr = addr << 9; //addr = addr * 512

	cmd[1] = ((addr & 0xFF000000) >>24 );
	cmd[2] = ((addr & 0x00FF0000) >>16 );
	cmd[3] = ((addr & 0x0000FF00) >>8 );
	cmd[4] = (addr &  0x000000FF);

	response=mmc_write_command (cmd);		// commando senden und response speichern

	return response;
}


// **********************************************************************************************************************************
//multi block schreiben von sektoren. bei aufruf wird immer ein sektor geschrieben immer der reihe nach
// **********************************************************************************************************************************
uint8_t mmc_multi_block_write_sector (uint8_t *Buffer){

	uint16_t a;			// einfacher zaehler fuer bytes eines sektors
	uint8_t response;

	mmc_write_byte(0xFC);

	// mmc/sd in hardware spi, block schreiben
	#if (MMC_SOFT_SPI==FALSE)
		uint8_t tmp;			// hilfs variable zur optimierung
		a=512;				// do while konstrukt weils schneller geht
		tmp=*Buffer;			// holt neues byte aus ram in register
		Buffer++;			// zeigt auf naechstes byte
		do{
			SPDR = tmp;    //Sendet ein Byte
			tmp=*Buffer;	// holt schonmal neues aus ram in register
			Buffer++;
			loop_until_bit_is_set(SPSR,SPIF);
		}while(--a);

	// mmc/sd in software spi, block schreiben
	#else
		a=512;
		do{
			mmc_write_byte(*Buffer++);
		}while(--a);
	#endif

	//CRC-Bytes schreiben
	mmc_write_byte(0xFF); //Schreibt Dummy CRC
	mmc_write_byte(0xFF); //CRC Code wird nicht benutzt

	response=mmc_read_byte();

	mmc_wait_ready();

	if ((response&0x1F) == 0x05 ){			// daten von der karte angenommen, alles ok.
		return TRUE;
	}

	return FALSE;							// daten nicht angenommen... hiernach muss stop token gesendet werden !

}



#endif

// **********************************************************************************************************************************
// wartet darauf, dass die mmc karte in idle geht
// **********************************************************************************************************************************
static uint8_t mmc_wait_ready (void){


	while(1)
	{
		if(	 spi_read_byte() == 0xFF ) return TRUE;
	}

	return FALSE;
}




// **********************************************************************************************************************************
// Routine zum schreiben eines Blocks(512Byte) auf die MMC/SD-Karte
// **********************************************************************************************************************************
uint8_t mmc_write_sector (uint32_t addr,uint8_t *buffer){

	uint8_t resp;
	uint8_t retrys;
	uint16_t count;
   	
	if ( !(fat.card_type & CT_BLOCK) ){
		addr *= 512;				// Convert to byte address if needed 
	}
	
	if ( mmc_send_cmd(CMD24, addr) != 0){ 	// enables card		
		return FALSE;
	}

	if ( FALSE == mmc_wait_ready() ){		
		return FALSE;
	}

	spi_write_byte(0xFE);			// Xmit data token 
	
	count = 512;
	do {							// Xmit the 512 byte data block to MMC 
		spi_write_byte(*buffer++);		
	} while (--count);
	
	spi_write_byte(0xFF);			// CRC (Dummy) 
	spi_write_byte(0xFF);
	
	retrys = 20;			
	do{
		resp = spi_read_byte();		// Reveive data response, 20 retrys if not acepted
	}while( (resp & 0x1F) != 0x05 && --retrys);
	
	if ( retrys == 0){				// If not accepted, return with error 		
		return FALSE;
	}
	
	mmc_disable();

	return TRUE;
}


// **********************************************************************************************************************************
// Routine zum lesen eines Blocks(512Byte) von der MMC/SD-Karte
// **********************************************************************************************************************************
uint8_t mmc_read_sector (uint32_t addr,uint8_t *buffer){

	uint8_t token;
	uint16_t count;
	
	if ( !(fat.card_type & CT_BLOCK) ) addr *= 512;	// Convert to byte address if needed

	if ( mmc_send_cmd(CMD17, addr) != 0 ){
		return FALSE;	
	}

	do {							// Wait for data packet in timeout of 200ms 
		token = spi_read_byte();
	} while (token == 0xFF);
	
	if(token != 0xFE){
		return FALSE;				// If not valid data token, retutn with error 
	}

	count = 512;
	do {							// Receive the data block into buffer 
		*buffer++ = spi_read_byte();
	} while (--count);

	spi_read_byte();				// Discard CRC 
	spi_read_byte();

	mmc_disable();

	return TRUE;					// Return with success 
}



// **********************************************************************************************************************************
#if(MMC_STATUS_INFO == TRUE)
uint8_t mmc_present(void) {
	  return get_pin_present() == 0x00;
  }
#endif


// **********************************************************************************************************************************
#if(MMC_STATUS_INFO == TRUE && MMC_WRITE == TRUE)
uint8_t mmc_protected(void) {
	  return get_pin_protected() != 0x00;
  }
#endif



