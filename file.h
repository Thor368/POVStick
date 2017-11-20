/*
 * 	Doku, siehe http://www.mikrocontroller.net/articles/AVR_FAT32
 *  Neuste Version: http://www.mikrocontroller.net/svnbrowser/avr-fat32/
 *	Autor: Daniel R.
 */


#ifndef _FILE_H

  #define _FILE_H

  //#######################################################################################################################
  // funktionen

  extern uint8_t 	ffread(void);						// liest byte-weise aus der datei (puffert immer 512 bytes zwischen)
  extern void 		ffwrite( uint8_t c);			// schreibt ein byte in die geoeffnete datei
  extern void 		ffwrites( uint8_t *s );		// schreibt string auf karte
  extern void 		ffwriten( uint8_t *s, uint16_t n ); // schreibt n bytes aus s auf die karte. maximal 2^16 stueck wegen datentyp von n !
  extern uint8_t 	ffopen( uint8_t name[], uint8_t rw_flag);	// kann immer nur 1 datei bearbeiten.
  extern uint8_t 	ffclose(void);						// muss aufgerufen werden bevor neue datei bearbeitet wird.
  extern void 		ffseek(uint32_t offset);	// setzt zeiger:bytesOfSec auf position in der geöffneten datei.
  extern uint8_t 	ffcd( uint8_t name[]);		// wechselt direktory
  extern void 		ffls(fptr_t uputs_ptr);				// zeigt direktory inhalt an, muss zeiger auf eine ausgabe funktion uebergeben bekommen
  extern uint8_t 	ffcdLower(void);					// geht ein direktory zurueck, also cd.. (parent direktory)
  extern uint8_t 	ffrm( uint8_t name[]);		// loescht datei aus aktuellem verzeichniss.
  extern void 		ffmkdir( uint8_t name[]); 	// legt ordner in aktuellem verzeichniss an.
  extern void 		fflushFileData(void);				// updatet datei informationen. sichert alle noetigen informationen!
  extern uint8_t 	ffileExsists ( uint8_t name[]); // prueft ob es die datei im aktuellen verzeichnis gibt. ffopen wuerde die datei direkt anlegen falls es sie noch nicht gibt!
  
  //#######################################################################################################################
  



#endif
