/*
 * 	Doku, siehe http://www.mikrocontroller.net/articles/AVR_FAT32
 *  Neuste Version: http://www.mikrocontroller.net/svnbrowser/avr-fat32/
 *	Autor: Daniel R.
 */

#include <string.h>


#include "mmc_config.h"
#include "fat.h"
#include "file.h"
#include "mmc.h"

struct Fat_t fat;   		// wichtige daten/variablen der fat
struct Chain_t chain;		// zum verketten undso

//***************************************************************************************************************
// funktionsprototypen die nur in dieser datei benutzt werden und auch nur bedingt mitkompiliert werden!
static uint8_t 	fat_loadFileDataFromCluster(uint32_t sec ,  uint8_t name []);
#if (MMC_WRITE==TRUE)
	static void 			fat_getFreeRowsOfDir(uint32_t dir,uint8_t row_cnt);
	static uint8_t 	fat_getFreeRowsOfCluster(uint32_t secStart, uint8_t rows);
	static void 			fat_addClusterToDir(uint32_t lastClusterOfDir);
	#if (MMC_LFN_SUPPORT==TRUE)
		static void 			fat_makeLfnDataEntrys(uint8_t name[],uint8_t row_count);
		static uint8_t 	fat_lfn_checksum(uint8_t name[]);
	#endif
#endif
#if (MMC_TIME_STAMP==TRUE)
	static  uint16_t 	fat_getTime(void); // infos, siehe config.h in sektion: SCHALTER und ZEIT FUNKTIONEN
	static  uint16_t 	fat_getDate(void);
#endif
//***************************************************************************************************************


#if (MMC_WRITE==TRUE)
//***************************************************************************************************************
// schreibt sektor nummer:sec auf die karte (puffer fat.sector) !!
// setzt bufferFlag=0 da puffer nicht dirty sein kann nach schreiben !
//***************************************************************************************************************
uint8_t fat_writeSector(uint32_t sec){
 
	fat.bufferDirty = FALSE;						// buffer kann nicht dirty sein weil wird geschrieben
	//printf("\nw_Sec = %lu",sec);
	return (mmc_write_sector(sec,fat.sector));		// schreiben von sektor puffer
}
#endif


//***************************************************************************************************************
// laed sektor:sec auf puffer:sector zum bearbeiten im ram !
// setzt currentSectorNr auf richtigen wert (also den sektor der gepuffert ist). es wird geprueft
// ob der gepufferte sektor geändert wurde, wenn ja muss erst geschrieben werden, um diese daten nicht zu verlieren !
//***************************************************************************************************************
uint8_t fat_loadSector(uint32_t sec){
	
	if( sec != file.currentSectorNr){			// nachladen noetig
		#if (MMC_WRITE==TRUE)
			if( fat.bufferDirty == TRUE ) {
				fat.bufferDirty = FALSE;		// buffer kann nicht dirty sein weil wird geschrieben
				mmc_write_sector( file.currentSectorNr,fat.sector );			// schreiben von sektor puffer
			}
		#endif
		fat.lastSector = file.currentSectorNr;	// den alten sektor sichern
		mmc_read_sector( sec,fat.sector );		// neuen sektor laden
		file.currentSectorNr = sec;				// aktualisiert sektor nummer (nummer des gepufferten sektors)
		return TRUE;
	}	
		
	else return TRUE;							// alles ok, daten sind schon da (sec==fat.currentSectorNr)

}


// ***************************************************************************************************************
// umrechnung cluster auf 1.sektor des clusters (möglicherweise mehrere sektoren/cluster) !
// ***************************************************************************************************************
uint32_t fat_clustToSec(uint32_t clust){

	return fat.dataDirSec + ( (clust - 2) * fat.secPerClust );		// errechnet den 1. sektor der sektoren des clusters
}


// ***************************************************************************************************************
// umrechnung sektor auf cluster (nicht die position im cluster selber!!)
// ***************************************************************************************************************
uint32_t fat_secToClust(uint32_t sec){

  return ( (sec - fat.dataDirSec) / fat.secPerClust ) + 2;			// umkerhrfunktion von fat_clustToSec
}







// datei lesen funktionen:


//***************************************************************************************************************
// laed die reihe:row des gepufferten sektors auf das struct:file. dort stehen dann
// alle wichgigen daten wie: 1.cluster,länge bei dateien, name des eintrags, reihen nummer (im sektor), attribut use...
//***************************************************************************************************************
void fat_loadRowOfSector(uint16_t row){

	#if (MMC_ENDIANNESS_LITTLE==TRUE)
		void *vsector;									// void-pointer, damit man schoen umbiegen kann :)

		vsector=&fat.sector[row+20];					// row ist byteoffset einer reihe

		file.firstCluster=*(uint16_t*)vsector;	// high word von first.cluster (20,2)
		file.firstCluster=file.firstCluster<<16;
		
		vsector=&fat.sector[row+26];
		file.firstCluster|=*(uint16_t*)vsector;	// low word von first.cluster (26,2)
		
		vsector=&fat.sector[row+28];
		file.length=*(uint32_t*)vsector;		// 4 byte von file.length (28,4)
	#else
		uint8_t *psector =& fat.sector[row+31];

		file.length =  *psector--;		// 31		hoechstes byte
		file.length <<= 8;
		file.length |=  *psector--;		// 30
		file.length <<= 8;
		file.length |=  *psector--;		// 29
		file.length <<= 8;
		file.length |=  *psector;		// 28

		psector-=7;
		file.firstCluster =  *psector--;					// 21	hoechstes byte
		file.firstCluster <<= 8;
		file.firstCluster = file.firstCluster | *psector;	// 20
		file.firstCluster <<= 8;

		psector+=7;
		file.firstCluster = file.firstCluster | *psector--;	// 27
		file.firstCluster <<= 8;
		file.firstCluster = file.firstCluster | *psector;	// 26
	#endif
}


#if (MMC_LFN_SUPPORT==TRUE)
//***************************************************************************************************************
// checksumme fuer den kurzen dateieintrag
//***************************************************************************************************************
static uint8_t fat_lfn_checksum(uint8_t name[])
{
	uint8_t cnt;
	uint8_t sum;

	cnt=11;
	sum=0;
	do{
		sum = ((sum & 1) << 7) + (sum >> 1) + *name++;
	}while(--cnt);

	return sum;
}


//***************************************************************************************************************
// prueft auf lange dateinamen (lfn), ermittelt anhand des langen dateinamens den kurzen. nur der kurze
// enthaelt die noetigen informationen um die datei lesen zu koennen. ist der gesuchte dateiname gefunden
// aber im naechsten sektor/cluster ermittelt die funktion selbstaendig diese und holt die informationen !
//***************************************************************************************************************
static uint8_t fat_loadFileDataFromCluster(uint32_t sec , uint8_t name []){

    uint16_t row;    // um durch zeilen zu gehen, eigentlich erstes byte einer reihe...
    uint8_t sectors;  // um durch sektoren zu zaehlen, sectors+sec = tatsaechliche sektornummer (absoluter sektor)
    uint8_t i;      // um durch eine zeile/reihe zu laufen
    uint8_t j;      // laufvariable fuer sfn

    // diese variablen muessen statisch sein, weil ein lfn eintrag auf 2 cluter aufgeteilt sein kann !
    static uint8_t checksum = 0;  // die kurze dateinamen checksumme, die ausgelesene.
    static enum flags { wait=0,start,readout } lfn_state;
    static uint8_t match = 0;    // treffer bei datei namen vergleich zaehlen

    const uint8_t map[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};    // index von map ist index des zu pruefenden bytes und inhalt ist index des zu pruefenden bytes in der reihe
    const uint8_t name_length = strlen((char *)name)-1;      // es wird die laenge des dateinamen zum vergleichen benoetigt!

    sectors = 0;

    // 5 moegliche zustaende im inneren der beiden schleifen...
    do{                      // sektoren des clusters pruefen
    row=0;                    // neuer sektor, dann reihen von 0 an.
    mmc_read_sector(sec+sectors,fat.sector);  // laed den sektor sec auf den puffer fat.sector
    fat.lastSector = file.currentSectorNr;    // sichern der alten sektor nummer
    file.currentSectorNr = sec + sectors;    // setzen des aktuellen sektors
    do{                      // reihen des sektors pruefen
      // 1.) nach einem eintrag mit 0x00 kommt nix mehr => restlicher cluster leer!
      if( fat.sector[row]==0x00 ){
        return FALSE;
      }
      // 2.1) ist eintrag ein lfn eintrag?   siehe: http://en.wikipedia.org/wiki/File_Allocation_Table#Long_file_names
      if( (fat.sector[row]&0x40) == 0x40 && fat.sector[row+11] == 0x0F && fat.sector[row] != 0xE5 ){                                      // ist lfn eintrag, jetzt pruefen...
        lfn_state = start;
      }
      // 2.2) ist eintrag ein sfn eintrag? 
      if( (fat.sector[row+11] == 0x10 || fat.sector[row+11] == 0x20) && fat.sector[row] != 0xE5 && lfn_state != readout){
        // vergleich von sfn dateinamen
        i = 0; j = 0;
        do{          
          if( fat.sector[row+i] == 0x20 ){     // ueberspringen von leerzeichen auf der karte
            i++; continue;
          }
          if( name[j] == '.' ){           // ueberspringen von punkt im dateinamen
            if( i <= 7) break;
            j++; continue;
          }
          if( fat.sector[row+i] != toupper(name[j]) )  break;
          i++; j++;
        }while(i<11);
        // datei gefunden
        if( i == 11 && j != 11){
          lfn_state = readout;                                      // ist sfn eintrag jetzt auslesen
          checksum = fat_lfn_checksum( &fat.sector[row] );
        }
      } 
      // 3.) lfn gefunden, jetzt verarbeiten. raussuchen der richtigen bytes und direkter vergleich mit dem original.
      if( lfn_state == start || match !=0 ){
        i=12;
        do{
          if( fat.sector[row+map[i]]!=0x00 && fat.sector[row+map[i]]!=0xFF ){  // gueltiger buchstabe. ist gueltig wenn, es nicht 0 oder ff ist.
            if( fat.sector[row+map[i]] == name[name_length-match] ){    // vergleich mit original, ist treffer?
              match += 1;                          // bei treffer index zaehler fuer vergleich hochzaehlen
            }
            else {            // wenn ein gueltiger buchstabe keinen treffer verursacht, ist es die falsche datei !
              lfn_state = wait;     // zurueck zu ausgangszustand
              match = 0;        // treffer zurueck setzen
              break;          // weitere pruefung nicht noetig da eintraege ungleich!
            }
          }
        }while(i--);            // zeichen index einer zeile/reihe abarbeiten
        // komplette uebereinstimmung und sequenz id 1 ==> datei gefunden !!
        if( (name_length-match) == -1 && (fat.sector[row]&0x01) == 0x01){
          lfn_state = readout;      // langer dateiname stimmt ueberein, jetzt zustand wechseln.
          match = 0;            // vergleich index zurueck setzen, damit nicht wieder in diesen zustand gewechselt wird.
          checksum = fat.sector[row+13];  // checksumme sichern zum vergleich mit der errechneten anhand des kurzen dateinamens. in naechstem zustand vergleich.
        }
      }
      // 4.) gesuchte reihe auf stuct file laden zum weiter verarbeiten. das ist der sfn eintrag, mit dem firstCluster, der laenge usw...
      if( lfn_state == readout && fat.sector[row+11] != 0x0F ){
        lfn_state = wait;
        fat_loadRowOfSector(row);
        file.row = row>>5;                  // reihe sichern, ist fuer ffrm wichtig
        if(checksum==fat_lfn_checksum(&fat.sector[row])){  // pruefen ob checksumme stimmt...wenn ja alles klar :)
          file.entrySector = file.currentSectorNr;    // sichern des sektors in dem der sfn dateieintrag steht.
          return TRUE;
        }
      }

      }while( (row+=32) < 512 );      // mit row+32 springt man immer an die 1. stelle einer reihe (zeile) und das durch einen ganzen sektor
    }while( ++sectors < fat.secPerClust );  // geht durch sektoren des clusters

    return FALSE;              // datei nicht gefunden, zumindest in diesem cluster...
}


#else
//***************************************************************************************************************
// geht reihen weise durch sektoren des clusters mit dem startsektor:sec, und sucht nach der datei mit dem
// namen:name. es werden die einzelnen sektoren nachgeladen auf puffer:sector vor dem bearbeiten.
// wird die datei in dem cluster gefunden ist return 0 , sonst return1.
//***************************************************************************************************************
static uint8_t fat_loadFileDataFromCluster(uint32_t sec ,  uint8_t name []){

  uint16_t rows;		// um durch zeilen nummern zu zaehlen
  uint8_t sectors;		// um durch sektoren zu zaehlen
  uint8_t i;			// zaehler fuer datei namen vergleich
  uint8_t j;

  sectors = 0;
  do{										// sektoren des clusters pruefen
	rows = 0;								// neuer sektor, dann reihen von 0 an.
	mmc_read_sector( sec+sectors , fat.sector );	// laed den sektor sec auf den puffer fat.sector
	file.currentSectorNr = sec + sectors;	// setzen des aktuellen sektors
	do{										// reihen des sektors pruefen

		if( fat.sector[rows] == 0 ){		// wenn man auf erste 0 stoesst muesste der rest auch leer sein!
			return FALSE;
			}
		// normaler eintrag, ordner oder datei.
		if( (fat.sector[rows+11] == 0x10 || fat.sector[rows+11] == 0x20) && fat.sector[rows] != 0xE5 ){
			// vergleich von sfn dateinamen
			i = 0; j = 0;
			do{					
				if( fat.sector[rows+i] == 0x20 ){ 		// ueberspringen von leerzeichen auf der karte
					i++; continue;
				}
				if( name[j] == '.' ){ 					// ueberspringen von punkt im dateinamen
					if( i <= 7) break;
					j++; continue;
				}
				if( fat.sector[rows+i] != toupper(name[j]) )	break;
				j++;i++;
			}while(i<11);
			// datei gefunden
			if( i == 11 ){
				file.row = rows>>5;								// zeile sichern.
				fat_loadRowOfSector(rows);						// datei infos auf struct laden
				file.entrySector = file.currentSectorNr;		   	// sektor in dem die datei infos stehen sichern
		  		return TRUE;
		  	}																			// ist lfn eintrag, jetzt pruefen...
		}


		}while( (rows+=32) < 512 );			// springt immer auf zeilenanfang eines 32 byte eintrags im sektor
	}while(++sectors<fat.secPerClust);		// geht durch sektoren des clusters

	return FALSE;							// fehler (datei nicht gefunden, oder fehler beim lesen)
}
#endif

//***************************************************************************************************************
// wenn dir == 0 dann wird das root direktory durchsucht, wenn nicht wird der ordner cluster-chain gefolgt, um
// die datei zu finden. es wird das komplette directory in dem man sich befindet durchsucht.
// bei fat16 wird der rootDir berreich durchsucht, bei fat32 die cluster chain des rootDir.
//***************************************************************************************************************
uint8_t fat_loadFileDataFromDir( uint8_t name []){

	uint8_t	 	sectors;	// variable um durch root-dir sektoren zu zaehlen bei fat16
	uint32_t 	clusters;	// variable um durch cluster des verzeichnisses zu gehen

	// root-dir fat16 nach eintrag durchsuchen. es bleiben noch 3 moeglichkeiten: nicht root-dir fat16, nicht root-dir fat32 und root-dir fat32
	if(fat.dir==0 && fat.fatType==16){
		sectors = 0;
		do{
			// eintrag gefunden?
			if(TRUE==fat_loadFileDataFromCluster( fat.rootDir+sectors , name)) return TRUE;
			sectors += fat.secPerClust;
		}while( sectors < (uint8_t)32 );
	}

	// root-dir fat32 oder nicht root-dir fat32/16, nach eintrag durchsuchen
	else {
		// bestimmen ab welchem cluster nach eintrag gesucht werden soll
		clusters = fat.dir==0?fat.rootDir:fat.dir;

		// durch cluster des verzeichnisses gehen und ueberpruefen ob eintrag vorhanden
		while(!((clusters>=0x0ffffff8&&fat.fatType==32)||(clusters>=0xfff8&&fat.fatType==16))){// prueft ob weitere sektoren zum lesen da sind (fat32||fat16)
			if(TRUE==fat_loadFileDataFromCluster( fat_clustToSec(clusters) , name)) return TRUE;		 // daten der datei auf struct:file. datei gefunden (umrechnung auf absoluten sektor)
			clusters = fat_getNextCluster(clusters);									// liest naechsten cluster des dir-eintrags (unterverzeichniss groeßer 16 einträge)
		}
	}

	return FALSE;																// datei/verzeichniss nicht gefunden
}




// datei anlegen funktionen :


#if (MMC_WRITE==TRUE)
//***************************************************************************************************************
// macht den datei eintrag im jetzigen verzeichniss (fat.dir).
// file.row enthaelt die reihen nummer des leeren eintrags, der vorher gesucht wurde, auf puffer:sector ist der gewuenschte
// sektor gepuffert. fuer fat16 im root dir muss andere funktion genutzt werden, als fat_getFreeRowOfDir (durchsucht nur dirs).
// fat.rootDir enthält bei fat32 den start cluster des directory, bei fat16 den 1. sektor des rootDir bereichs!
//***************************************************************************************************************
void fat_makeFileEntry( uint8_t name [],uint8_t attrib){

	uint8_t sectors;				// um bei fat16 root dir sektoren zu zaehlen
	uint32_t dir;				// zum bestimmen ob im root-dir oder woanders

	#if (MMC_LFN_SUPPORT==TRUE)
		uint8_t need_rows;		// reihen die benoetigt werden um lfn und sfn zu speichern
		uint8_t name_length; 	// laenge des langen dateinamen
		name_length = strlen((char *)name);
		// berechnung wieviele reihen fuer den eintrag, also die lfn und den sfn, benoetigt werden.
		need_rows = (name_length % 13 == 0) ? name_length/13 + 1 : name_length/13  + 2;
	#endif

	// bestimmen in welchem cluster nach freien eintraegen gesucht werden soll
	dir = (fat.dir==0) ? fat.rootDir : fat.dir;

	// nach leeren eintraegen im root-dir suchen, nur wenn fat16
	if( fat.fatType == 16 && fat.dir == 0){
		sectors = 0;
		do{
			#if (MMC_LFN_SUPPORT==TRUE)
				if(TRUE==fat_getFreeRowsOfCluster( dir + sectors,need_rows) )break;	// freien eintrag im fat16 root dir gefunden?
			#else
				if(TRUE==fat_getFreeRowsOfCluster( dir + sectors,1) )break;		// freien eintrag im fat16 root dir gefunden?
			#endif
			sectors += fat.secPerClust;
		}while( sectors < (uint8_t)32  );
	}

	// wenn oben fat16 und root-dir abgehandelt wurde, bleiben noch 3 moeglichkeiten: root-dir fat32, nicht root-dir fat32 und nicht root-dir fat16
	#if (MMC_LFN_SUPPORT==TRUE)
		else fat_getFreeRowsOfDir(dir,need_rows);
	#else
		else fat_getFreeRowsOfDir(dir,1);
	#endif

	// setzten der noetigen werte der datei !
	file.entrySector = file.currentSectorNr;							// sichern des sektors in dem der sfn dateieintrag steht.
	file.firstCluster = fat_secToClust(chain.startSectors);
	chain.lastCluster = file.firstCluster;
	file.length = 0;

	// eintraege machen. sfn = short file name, lfn = long file name
	fat_makeSfnDataEntry(name,attrib,file.firstCluster,0);
	#if (MMC_LFN_SUPPORT==FALSE)
		mmc_write_sector(file.currentSectorNr,fat.sector);			// wenn nur sfn eintrag, dann jetzt hier sektor mit sfn eintrag schreiben...
	#else
		fat_makeLfnDataEntrys( name,need_rows-1);
	#endif

	// setzten des daten sektors der datei, damit jetzt darauf geschrieben werden kann
	file.currentSectorNr = chain.startSectors;
}


#if (MMC_ENDIANNESS_LITTLE==TRUE)
//***************************************************************************************************************
// erstellt 32 byte eintrag einer datei, oder verzeichnisses im puffer:sector.
// erstellt eintrag in reihe:row, mit namen:name usw... !!
// muss noch auf die karte geschrieben werden ! nicht optimiert auf geschwindigkeit.
//***************************************************************************************************************
void fat_makeSfnDataEntry(uint8_t name [],uint8_t attrib,uint32_t cluster,uint32_t length){

	uint8_t i,j; 		// byte zaehler in reihe von sektor (32byte eintrag)
	void *vsector; 				// void zeiger auf sektor, um beliebig casten zu können
	uint16_t row;		// reihe in dem sektor

	fat.bufferDirty = TRUE; 	// puffer beschrieben, also neue daten darin(vor lesen muss geschrieben werden)
	row = file.row;
	row = row<<5;				// multipliziert mit 32 um immer auf zeilen anfang zu kommen (zeile 0=0,zeile 1=32,zeile 2=62 ... zeile 15=480)
	vsector =& fat.sector[row];	// anfangs adresse holen ab der stelle auf sector geschrieben werden soll

	#if (MMC_TIME_STAMP==TRUE)
		uint8_t new=FALSE;
		// pruefung ob eintrag neu ist.
		if(fat.sector[row]==0xE5||fat.sector[row]==0x00)	 new=TRUE;
	#endif

	#if (MMC_TIME_STAMP==FALSE)
		// alle felder nullen...
		i = 20;
		do{
			*(uint8_t*)vsector++ = 0x00;
		}while( i-- );
		vsector =& fat.sector[row];
	#endif

	// namen schreiben (0,10)
	i=0; j=0;
	do{

		*(uint8_t*)vsector = 0x20;

		if( i < 8 && name[j] != '.'){
			*(uint8_t*)vsector = toupper(name[j]);
			j++;
		}

		j = ( i==8 ) ? j+1 : j;

		if( i >= 8 && name[j] != '\0'){
			*(uint8_t*)vsector = toupper(name[j]);
			j++;
		}
		vsector++;
	}while( ++i < 11);

	// attrib schreiben (11,1)
	*(uint8_t*)vsector = attrib;
	vsector++;

	#if (MMC_TIME_STAMP==TRUE)
		uint16_t time=fat_getTime();
		uint16_t date=fat_getDate();

		// reserviertes byte und milisekunden der erstellung (12,2)
		*(uint16_t*)vsector=0x0000;
		vsector+=2;

		if(new==TRUE){
			// creation time,date (14,4)
			*(uint32_t*)vsector=date;
			*(uint32_t*)vsector=*(uint32_t*)vsector<<16;
			*(uint32_t*)vsector|=time;
		}
		vsector+=4;

		// last access date (18,2)
		*(uint16_t*)vsector=date;
		vsector+=2;

		// low word von cluster (20,2)
		*(uint16_t*)vsector=(cluster&0xffff0000)>>16;
		vsector+=2;

		// last write time,date (22,4)
		*(uint32_t*)vsector=date;
		*(uint32_t*)vsector=*(uint32_t*)vsector<<16;
		*(uint32_t*)vsector|=time;
		vsector+=4;

		// high word von cluster (26,2)
		*(uint16_t*)vsector=(cluster&0x0000ffff);
		vsector+=2;

		// laenge (28,4)
		*(uint32_t*)vsector=length;
	#else
		vsector+=8;

		// low word	von cluster (20,2)
		*(uint16_t*)vsector=(cluster&0xffff0000)>>16;
		vsector+=6;

		// high word von cluster (26,2)
		*(uint16_t*)vsector=(cluster&0x0000ffff);
		vsector+=2;

		// laenge (28,4)
		*(uint32_t*)vsector=length;
	#endif
}
#else
//***************************************************************************************************************
// erstellt 32 byte eintrag einer datei, oder verzeichnisses im puffer:sector.
// erstellt eintrag in reihe:row, mit namen:name usw... !!
// muss noch auf die karte geschrieben werden ! nicht optimiert auf geschwindigkeit.
//***************************************************************************************************************
void fat_makeSfnDataEntry(uint8_t name [],uint8_t attrib,uint32_t cluster,uint32_t length){

	uint8_t i; 			// byte zaehler in reihe von sektor (32byte eintrag)
	uint8_t j;
	uint16_t row;
	uint8_t *psector;

	fat.bufferDirty = TRUE; 		// puffer beschrieben, also neue daten darin(vor lesen muss geschrieben werden)
	row = file.row;
	row = row<<5;					// multipliziert mit 32 um immer auf zeilen anfang zu kommen (zeile 0=0,zeile 1=32,zeile 2=62 ... zeile 15=480)
	psector =& fat.sector[row];

	#if (MMC_TIME_STAMP==TRUE)
		uint8_t new = FALSE;
		// pruefung ob eintrag neu ist.
		if(*psector==0xE5||*psector==0x00)	 new=TRUE;
	#endif

	#if (MMC_TIME_STAMP==FALSE)
		// alle felder nullen...
		i = 0;
		do{
			*psector++ = 0x00;
		}while( ++i < 32);
		psector =& fat.sector[row];
	#endif

	// namen schreiben (0,10)
	i = 0; j = 0;
	do{
		*psector = 0x20;

		if( i < 8 && name[j] != '.'){
			*psector = toupper(name[j]);
			j++;
		}

		j = ( i==8 ) ? j+1 : j;

		if( i >= 8 && name[j] != '\0'){
			*psector = toupper(name[j]);
			j++;
		}
		psector++;
	}while( ++i < 11);

	// attrib schreiben (11,1)
	*psector++ = attrib;

	#if (MMC_TIME_STAMP==TRUE)
		uint16_t time=fat_getTime();
		uint16_t date=fat_getDate();

		// reserviertes byte und milisekunden der erstellung (12,2)
		*psector++=0x00;
		*psector++=0x00;

		if(new==TRUE){
			// creation time,date (14,4)
			*psector++ = time&0xFF;
			*psector++ = (time&0xFF00)>>8;

			*psector++ = date&0xFF;
			*psector++ = (date&0xFF00)>>8;
		}
		else psector+=4;

		// last access date (18,2)
		*psector++=(date&0xFF00) >> 8;
		*psector++= date&0xFF;

		// hi word	von cluster (20,2)
		*psector++=(cluster&0x00ff0000)>>16;
		*psector++=(cluster&0xff000000)>>24;

		// last write time (22,2)
		*psector++ = time&0xFF;
		*psector++ = (time&0xFF00)>>8;

		// last write date (24,2)
		*psector++ = date&0xFF;
		*psector++ = (date&0xFF00)>>8;

		// low word von cluster (26,2)
		*psector++=cluster&0x000000ff;
		*psector++=(cluster&0x0000ff00)>>8;

		// laenge (28,4)
		*psector++=length&0x000000ff;
		*psector++=(length&0x0000ff00)>>8;
		*psector++=(length&0x00ff0000)>>16;
		*psector=(length&0xff000000)>>24;

	#else
		psector += 8;

		// hi word	von cluster (20,2)
		*psector++=(cluster&0x00ff0000)>>16;
		*psector=(cluster&0xff000000)>>24;
		psector += 5;

		// low word von cluster (26,2)
		*psector++=cluster&0x000000ff;
		*psector++=(cluster&0x0000ff00)>>8;

		// laenge (28,4)
		*psector++=length&0x000000ff;
		*psector++=(length&0x0000ff00)>>8;
		*psector++=(length&0x00ff0000)>>16;
		*psector=(length&0xff000000)>>24;
	#endif
}
#endif

#if (MMC_LFN_SUPPORT==TRUE)
//***************************************************************************************************************
// macht lange dateieintraege
//***************************************************************************************************************
static void fat_makeLfnDataEntrys(uint8_t name[],uint8_t rows){

	uint8_t cnt;				// sequenz nummer des lfn eintrags
	uint8_t c;				// wird gebraucht um unnoetige felder zu bearbeiten
	uint8_t chkSum;			// sfn checksumme
	uint16_t offset;			// reihen offset im sektor
	uint8_t i;				// laufindex fuer name
	uint8_t j;				// laufindex fuer eine zeile
	const uint8_t map[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};	// index von map ist index des zu pruefenden bytes und inhalt ist index des zu pruefenden bytes in der reihe
	uint8_t name_length = strlen((char *)name);					// es wird die laenge des dateinamen zum vergleichen benoetigt!

	fat.bufferDirty = TRUE;			// der puffer wird veraendert...
	offset = file.row<<5;			// jetzt auf 1. byte des letzten eintrags, das ist eigentlich der sfn, deshalt wird gleich noch was abgezogen

	// gerade vorher ist der sfn eintrag auf den sektor geschrieben worden. daraus jetzt die checksumme berechnen.
	chkSum = fat_lfn_checksum( &fat.sector[offset] );

	if( offset == 0 ){						// es muss ein eintrag abgezogen werden, geht das ohne probleme? wenn man in reihe 0 ist liegt der vorherige eintrag in einem anderen sektor/cluster
		fat_loadSector(fat.lastSector);		// fat.lastSectorNr ist in der fat_loadFileDataFromCluster gesichert worden
		offset = 512;
	}

	// die schleife zaehlt die zeichen durch, behandelt -offset, die -sequenz und die -stelle in einer reihe
	for( i=0, cnt=1 ; i<name_length ; cnt++ ){
		offset -= 32;
		fat.sector[offset]=cnt;				// sequenz nummer, fortlaufend hochzaehlend
		fat.sector[offset+11]=0x0F;			// lfn attribut
		fat.sector[offset+12]=0x00;			// immer 0
		fat.sector[offset+13]=chkSum;		// sfn check summe
		fat.sector[offset+26]=0x00;			// immer 0
		fat.sector[offset+27]=0x00;			// immer 0

		// hier wird der eigentliche name geschrieben, mit dem array map, werden die moeglichen stellen gemapt.
		j=0;
		do{
			fat.sector[ offset + map[j]    ] = name[i];		// ein zeichen des namens, ueber das array map auf die richtige stelle gemapt
			fat.sector[ offset + map[j] +1 ] = 0x00;		// hier ist das zweite ascii zeichen
			i++;											// i ist die anzahl der geschriebenen datei zeichen
			j++;
		}while( j<13 && i<name_length );					// geht durch stellen an denen der dateiname steht

		// es ist moeglich, dass die freien eintraege in verschiedenen sektoren/clustern liegen, deshalb diese verrenkungen
		if( offset == 0 && i < name_length){
			fat_loadSector(fat.lastSector);					// fat.lastSectorNr ist in der fat_loadFileDataFromCluster gesichert worden
			offset = 512;
		}
	}

	fat.sector[offset] |= 0x40;								// letzten eintag mit bit 7 gesetzt markieren, zu der sequenz nummer

	// nicht benoetigte felder bearbeiten...
	c = 0x00;
	while( j < 13 ){
		fat.sector[ offset + map[j]     ] = c;
		fat.sector[ offset + map[j] + 1 ] = c;
		c = (j<11) ? 0xFF : c;
		j++;
	}

	fat_writeSector(file.currentSectorNr);					// schreibt puffer mit den letzten lfn eintraegen auf die karte...
}
#endif

// ***************************************************************************************************************
// durchsucht cluster nach freiem platz fuer die anzahl rows.
// ***************************************************************************************************************
static uint8_t fat_getFreeRowsOfCluster(uint32_t secStart, uint8_t rows){

	uint8_t sectors;			// variable zum zaehlen der sektoren eines clusters.
	uint16_t row;			// offset auf reihen anfang

	// variable muss statisch sein, wenn die anzahl der freien reihen die gesucht werden auf 2 cluster aufgeteilt sind !
	static uint8_t match = 0;

	sectors = 0;
	do{
		fat_loadSector( secStart + sectors );
		row = 0;
		do{
			if( fat.sector[row]==0x00 || fat.sector[row]==0xE5 ){ 	// prueft auf freihen eintrag (leer oder geloescht gefunden?).
				match += 1;
				if( match == rows ){								// fertig hier, noetige anzahl gefunden!
					file.row = row>>5; 								// byteoffset umrechnen zu reihe
					match = 0;
					return TRUE;
				}
			}
			else {								// kein freier eintrag, wenn bis hier nicht schon genuegend, dann sinds zuwenige
				match = 0;
			}
		}while( (row+=32) < 512 );				// geht durch reihen/zeilen eines sektors
	}while( ++sectors < fat.secPerClust );		// geht die sektoren des clusters durch (moeglicherweise auch nur 1. sektor).

	return FALSE;
}

// ***************************************************************************************************************
// sucht leeren eintrag (zeile) im cluster mit dem startsektor:secStart.
// wird dort kein freier eintrag gefunden ist return (1).
// wird ein freier eintrag gefunden, ist die position der freien reihe auf file.row abzulesen und return (0).
// der sektor mit der freien reihe ist auf dem puffer fat.sector gepuffert.
// ****************************************************************************************************************
static void fat_getFreeRowsOfDir(uint32_t dir, uint8_t rows){

	uint32_t lastCluster;
	uint32_t lastSector;

	// verzeichnis durchsuchen und pruefen ob genuegend platz fuer anzahl der benoetigten reihen ist.
	do{
		if( TRUE == fat_getFreeRowsOfCluster( fat_clustToSec(dir), rows ) ){ 			// freien platz gefunden!!
			return;
		}
		lastSector = file.currentSectorNr;
		lastCluster = dir;
		dir = fat_getNextCluster(dir);													// dir ist parameter der funktion und der startcluster des ordners/dirs
	}while( !((dir>=0x0ffffff8&&fat.fatType==32)||(dir>=0xfff8&&fat.fatType==16)) );	// geht durch ganzes dir


	// hier verzeichnis durchsucht und nicht genuegend platz gefunden, jetzt platzt schaffen. es wird ein neuer cluster zu dem dir verkettet und dieser aufbereitet!

	fat_addClusterToDir(lastCluster);
	chain.lastCluster = fat_secToClust(lastSector);
	fat_getFreeRowsOfCluster( file.currentSectorNr, rows );

	// pruefen ob mehr als ein cluster am stueck frei ist/war. wenn nicht, muessen neue freie fuer das beschreiben der datei gesucht werden, da ja gerade einer zum dir verkettet wurde
	if( chain.cntSecs>fat.secPerClust ){
		chain.startSectors += fat.secPerClust;
		chain.cntSecs -= fat.secPerClust;
	}
	else{
		fat_getFreeClustersInRow( lastCluster );
	}
}

// ***************************************************************************************************************
// verkettet einen neuen cluster zu einem vorhandenen verzeichniss. es muss der letzte bekannte cluster des
// verzeichnisses uebergeben werden und es muessen vorher neue freie gesucht worden sein.
// ****************************************************************************************************************
static void fat_addClusterToDir(uint32_t lastClusterOfDir){

	uint32_t newCluster;
	uint16_t j;
	uint8_t i;

	newCluster = fat_secToClust(chain.startSectors);

	// verketten des dirs mit dem neuen cluster (fat bereich)
	fat_setCluster(lastClusterOfDir,newCluster);
	fat_setCluster(newCluster,0x0fffffff);
	mmc_write_sector(file.currentSectorNr,fat.sector);

	// bereitet puffer so auf, dass die einzelnen sektoren des neuen clusters mit 0x00 initialisiert werden koennen.
	j = 511;
	do{
		fat.sector[j]=0x00;
	}while(j--);

	// jetzt sektoren des neuen clusters mit vorbereitetem puffer beschreiben (daten bereich)
	i = 0;
	do{
		fat_writeSector(chain.startSectors + i );		// nullen des clusters
	}while( i++ < fat.secPerClust);					// bis cluster komplett genullt wurde.

	// auf ersten sektor des clusters setzen
	file.currentSectorNr = chain.startSectors;
}
#endif





// fat funktionen:

//***************************************************************************************************************
// sucht folge Cluster aus der fat !
// erster daten cluster = 2, ende einer cluster chain 0xFFFF (fat16) oder 0xFFFFFFF (fat32),
// stelle des clusters in der fat, hat als wert, den nächsten cluster. (1:1 gemapt)!
//***************************************************************************************************************
uint32_t fat_getNextCluster(uint32_t oneCluster){
  
	uint32_t sector;

	#if (MMC_ENDIANNESS_LITTLE==TRUE)
		void *bytesOfSec;
	#else
		uint8_t *bytesOfSec;
	#endif

	// FAT 16
	if(fat.fatType==16){
		oneCluster = oneCluster << 1;
		sector = fat.fatSec + (oneCluster >> 9);
		fat_loadSector(sector);
		bytesOfSec =& fat.sector[oneCluster % 512];

		#if (MMC_ENDIANNESS_LITTLE==TRUE)
			return *(uint16_t*)bytesOfSec;
		#else
			bytesOfSec += 1;
			sector = *bytesOfSec--;
			sector <<= 8;
			sector |= *bytesOfSec;
			return sector;
		#endif
	}
	// FAT 32
	else{
		oneCluster = oneCluster << 2;
		sector = fat.fatSec + (oneCluster >> 9);
		fat_loadSector(sector);
		bytesOfSec =& fat.sector[oneCluster % 512];

		#if (MMC_ENDIANNESS_LITTLE==TRUE)
			return *(uint32_t*)bytesOfSec;
		#else
			bytesOfSec += 3;
			sector = *bytesOfSec--;
			sector <<= 8;
			sector |= *bytesOfSec--;
			sector <<= 8;
			sector |= *bytesOfSec--;
			sector <<= 8;
			sector |= *bytesOfSec;
			return sector;
		#endif
	}

}


//***************************************************************************************************************
// sucht verkettete cluster einer datei, die in einer reihe liegen. worst case: nur ein cluster.
// sieht in der fat ab dem cluster offsetCluster nach. sucht die anzahl von MAX_CLUSTERS_IN_ROW,
// am stueck,falls möglich. prueft ob der cluster neben offsetCluster dazu gehört...
// setzt dann fat.endSectors und fat.startSectors. das -1 weil z.b. [95,98] = {95,96,97,98} = 4 sektoren
//***************************************************************************************************************
void fat_getFatChainClustersInRow( uint32_t offsetCluster){

	uint16_t cnt = 0;

	chain.startSectors = fat_clustToSec(offsetCluster);		// setzen des 1. sektors der datei
	chain.cntSecs = fat.secPerClust;

	while( cnt<MMC_MAX_CLUSTERS_IN_ROW ){
		if( (offsetCluster+cnt+1)==fat_getNextCluster(offsetCluster+cnt) )		// zaehlen der zusammenhaengenden sektoren
			chain.cntSecs += fat.secPerClust;
		else {
			chain.lastCluster = offsetCluster+cnt;	// hier landet man, wenn es nicht MAX_CLUSTERS_IN_ROW am stueck gibt, also vorher ein nicht passender cluster gefunden wurde.
			return;
		}
		cnt+=1;
	}

	chain.lastCluster = offsetCluster+cnt;			// hier landet man, wenn MAX_CLUSTERS_IN_ROW gefunden wurden
}


#if (MMC_WRITE==TRUE)
//***************************************************************************************************************
// sucht freie zusammenhaengende cluster aus der fat. maximal MAX_CLUSTERS_IN_ROW am stueck.
// erst wir der erste frei cluster gesucht, ab offsetCluster(iklusive) und dann wird geschaut, ob der
// daneben auch frei ist. setzt dann fat.endSectors und chain.startSectors. das -1 weil z.b. [95,98] = {95,96,97,98} = 4 sektoren
//***************************************************************************************************************
void fat_getFreeClustersInRow(uint32_t offsetCluster){

	uint16_t cnt=1; 							// variable fuer anzahl der zu suchenden cluster

	while(fat_getNextCluster(offsetCluster)){		// suche des 1. freien clusters
		offsetCluster++;
	}

	chain.startSectors = fat_clustToSec(offsetCluster);	// setzen des startsektors der freien sektoren (umrechnen von cluster zu sektoren)
	chain.cntSecs = fat.secPerClust;					// da schonmal mindestens einer gefunden wurde kann hier auch schon cntSecs damit gesetzt werden

	do{													// suche der naechsten freien
		if(0==fat_getNextCluster(offsetCluster+cnt) )	// zaehlen der zusammenhängenden sektoren
			chain.cntSecs += fat.secPerClust;
		else{
			return;									// cluster daneben ist nicht frei
		}
		cnt++;
	}while( cnt<MMC_MAX_CLUSTERS_IN_ROW );			// wenn man hier raus rasselt, gibt es mehr freie zusammenhaengende als MAX_CLUSTERS_IN_ROW


}

 
//***************************************************************************************************************
// verkettet ab startCluster bis einschließlich endClu
// es ist wegen der fragmentierung der fat nötig, sich den letzten bekannten cluster zu merken, 
// damit man bei weiteren cluster in einer reihe die alten cluster noch dazu verketten kann (so sind luecken im verketten möglich).
//***************************************************************************************************************
void fat_setClusterChain(uint32_t startCluster, uint32_t endCluster){

  fat_setCluster( chain.lastCluster ,startCluster );	// ende der chain setzen, bzw verketten der ketten
  
  while( startCluster != endCluster ){
	 startCluster++;
	 fat_setCluster( startCluster-1 ,startCluster );// verketten der cluster der neuen kette
	 }
	 
  fat_setCluster( startCluster,0xfffffff );			// ende der chain setzen
  chain.lastCluster = endCluster;					// ende cluster der kette updaten
  fat.bufferDirty = FALSE;
  mmc_write_sector (file.currentSectorNr,fat.sector);
}


//***************************************************************************************************************
// setzt den cluster inhalt. errechnet den sektor der fat in dem cluster ist, errechnet das low byte von
// cluster und setzt dann byteweise den inhalt:content.
// prueft ob buffer dirty (zu setztender cluster nicht in jetzt gepuffertem).
// pruefung erfolgt in fat_loadSector, dann wird alter vorher geschrieben, sonst gehen dort daten verloren !!
//***************************************************************************************************************
void fat_setCluster( uint32_t cluster, uint32_t content){

	uint32_t sector;

	#if (MMC_ENDIANNESS_LITTLE==TRUE)
		void *bytesOfSec;
	#else
		uint8_t *bytesOfSec;
	#endif

	// FAT 16
	if(fat.fatType==16){
		cluster = cluster << 1;
		sector = fat.fatSec + (cluster >> 9);
		fat_loadSector(sector);
		bytesOfSec =& fat.sector[cluster % 512];

		#if (MMC_ENDIANNESS_LITTLE==TRUE)
			*(uint16_t*)bytesOfSec = content;
		#else
			*bytesOfSec++ = content;
			*bytesOfSec = content >> 8;
		#endif
	}
	// FAT 32
	else{
		cluster = cluster << 2;
		sector = fat.fatSec + (cluster >> 9);
		fat_loadSector(sector);
		bytesOfSec =& fat.sector[cluster % 512];

		#if (MMC_ENDIANNESS_LITTLE==TRUE)
			*(uint32_t*)bytesOfSec = content;
		#else
			*bytesOfSec++ = content;
			*bytesOfSec++ = content >> 8;
			*bytesOfSec++ = content >> 16;
			*bytesOfSec = content >> 24;
		#endif
	}

	fat.bufferDirty = TRUE;						// zeigt an, dass im aktuellen sector geschrieben wurde
}


//***************************************************************************************************************
// löscht cluster chain, beginnend ab dem startCluster.
// sucht cluster, setzt inhalt usw.. abschließend noch den cluster-chain ende markierten cluster löschen.
//***************************************************************************************************************
void fat_delClusterChain(uint32_t startCluster){

  uint32_t nextCluster = startCluster;		// tmp variable, wegen verketteter cluster..
	
  do{
	 startCluster = nextCluster;
	 nextCluster = fat_getNextCluster(startCluster);
	 fat_setCluster(startCluster,0x00000000);  	
  }while(!((nextCluster>=0x0ffffff8&&fat.fatType==32)||(nextCluster>=0xfff8&&fat.fatType==16)));

  fat_writeSector(file.currentSectorNr);
}
#endif


//***************************************************************************************************************
// Initialisiert die Fat(16/32) daten, wie: root directory sektor, daten sektor, fat sektor...
// siehe auch Fatgen103.pdf. ist NICHT auf performance optimiert!
// byte/sector, byte/cluster, anzahl der fats, sector/fat ... (halt alle wichtigen daten zum lesen ders datei systems!)
//*****************************************************************<**********************************************
uint8_t fat_loadFatData(void){
										// offset,size
	uint16_t  	rootEntCnt;		// 17,2				groesse die eine fat belegt
	uint16_t  	fatSz16;		// 22,2				sectors occupied by one fat16
	uint32_t 	fatSz32;		// 36,4				sectors occupied by one fat32
	uint32_t 	secOfFirstPartition;				// ist 1. sektor der 1. partition aus dem MBR
	#if (MMC_ENDIANNESS_LITTLE==TRUE)
		void *vsector;
	#endif

	if(TRUE==mmc_read_sector(0,fat.sector)){				//startsektor bestimmen
		secOfFirstPartition = 0;
		if( fat.sector[457] == 0 ){		
			#if (MMC_ENDIANNESS_LITTLE==TRUE)
				vsector =& fat.sector[454];
				secOfFirstPartition = *(uint32_t*)vsector;
			#else
				secOfFirstPartition |= fat.sector[456];
				secOfFirstPartition <<= 8;
	
				secOfFirstPartition |= fat.sector[455];
				secOfFirstPartition <<= 8;
	
				secOfFirstPartition |= fat.sector[454];
			#endif
			mmc_read_sector(secOfFirstPartition,fat.sector);		// ist kein superfloppy gewesen
		}
					
		fat.secPerClust=fat.sector[13];		// fat.secPerClust, 13 only (power of 2)

		#if (MMC_ENDIANNESS_LITTLE==TRUE)
			vsector =& fat.sector[14];
			fat.fatSec=*(uint16_t*)vsector;

			vsector=&fat.sector[17];
			rootEntCnt=*(uint16_t*)vsector;

			vsector=&fat.sector[22];
			fatSz16=*(uint16_t*)vsector;
		#else
			fat.fatSec = fat.sector[15];
			fat.fatSec <<= 8;
			fat.fatSec |= fat.sector[14];

			rootEntCnt = fat.sector[18];
			rootEntCnt <<= 8;
			rootEntCnt |= fat.sector[17];

			fatSz16 = fat.sector[23];
			fatSz16 <<= 8;
			fatSz16 |= fat.sector[22];
		#endif

		fat.rootDir	 = ( (rootEntCnt <<5) + 511 ) /512;	// ist 0 bei fat 32, sonst der root dir sektor

		if(fat.rootDir==0){									// FAT32 spezifisch (die pruefung so, ist nicht spezifikation konform !).
			#if (MMC_ENDIANNESS_LITTLE==TRUE)
				vsector=&fat.sector[36];
				fatSz32=*(uint32_t *)vsector;

				vsector=&fat.sector[44];
				fat.rootDir=*(uint32_t *)vsector;
			#else
				fatSz32 = fat.sector[39];
				fatSz32 <<= 8;
				fatSz32 |= fat.sector[38];
				fatSz32 <<= 8;
				fatSz32 |= fat.sector[37];
				fatSz32 <<= 8;
				fatSz32 |= fat.sector[36];

				fat.rootDir = fat.sector[47];
				fat.rootDir <<= 8;
				fat.rootDir |= fat.sector[46];
				fat.rootDir <<= 8;
				fat.rootDir |= fat.sector[45];
				fat.rootDir <<= 8;
				fat.rootDir |= fat.sector[44];
			#endif

			fat.dataDirSec = fat.fatSec + (fatSz32 * fat.sector[16]);	// data sector (beginnt mit cluster 2)
			fat.fatType=32;									// fat typ
			}

		else{												// FAT16	spezifisch
			fat.dataDirSec = fat.fatSec + (fatSz16 * fat.sector[16]) + fat.rootDir;		// data sektor (beginnt mit cluster 2)
			fat.rootDir=fat.dataDirSec-fat.rootDir;			// root dir sektor, da nicht im datenbereich (cluster)
			fat.rootDir+=secOfFirstPartition;				// addiert den startsektor auf 	"
			fat.fatType=16;									// fat typ
			}

		fat.fatSec+=secOfFirstPartition;					// addiert den startsektor auf
		fat.dataDirSec+=secOfFirstPartition;				// addiert den startsektor auf (umrechnung von absolut auf real)
		fat.dir=0;											// dir auf '0'==root dir, sonst 1.Cluster des dir
		return TRUE;
		}

return FALSE;			// sector nicht gelesen, fat nicht initialisiert!!
}


#if (MMC_TIME_STAMP==TRUE)
// *****************************************************************************************************************
// gibt akuelles datum zurueck
// *****************************************************************************************************************
static uint16_t fat_getDate(void){
	// rueckgabe wert in folgendem format:
	// bits [0-4]  => tag es monats, gueltige werte: 1-31
	// bits [5-8]  => monat des jahres, gueltige werte: 1-12
	// bits [9-15] => anzahl jahre seit 1980, gueltige werte: 0-127 (moegliche jahre demnach: 1980-2107)
	// macht insgesammt 16 bit also eine 2 byte variable !

	uint16_t date=0;
	//    Tag Monat   Jahr (29=2009-1980)
	date|=22|(12<<5)|(29<<9);
	// bei diesem beispiel => 22.12.2009 	(29=2009-1980)

	// hier muss code hin, der richtige werte auf date erstellt, so wie beim beispiel !!

	return date;
}


// *****************************************************************************************************************
// gibt aktuelle zeit zurueck
// *****************************************************************************************************************
static uint16_t fat_getTime(void){
	// rueckgabe wert in folgendem format:
	// bits [0-4]   => sekunde, gueltige werte: 0-29, laut spezifikation wird diese zahl beim anzeigen mit 2 multipliziert, womit man dann auf eine anzeige von 0-58 sekunden kommt !
	// bits [5-10]  => minuten, gueltige werte: 0-59
	// bits [11-15] => stunden, gueltige werte: 0-23
	// macht insgesammt 16 bit also eine 2 byte variable !


	uint16_t time=0;
	//  Sek. Minute  Stunde		Sek wird beim anzeigen noch mit 2 multipliziert.
	time|=10|(58<<5)|(16<<11);
	// bei diesem Beispiel=> 16:58 und 20 sekunden (20=10*2 muss so laut spezifikation !)

	// hier muss code hin, der richtige werte auf time erstellt, so wie beim beispiel gezeigt !!

	return time;
}
#endif


#if (MMC_GET_FREE_BYTES==TRUE)
// *****************************************************************************************************************
// zaehlt die freien cluster in der fat. gibt die anzahl der freien bytes zurueck !
// vorsicht, kann lange dauern !
// ist nicht 100% exakt, es werden bis zu ein paar hundert bytes nicht mit gezaehlt, da man lieber ein paar bytes verschwenden
// sollte, als zu versuchen auf bytes zu schreiben die nicht da sind ;)
// *****************************************************************************************************************
uint64_t fat_getFreeBytes(void){
	uint64_t bytes;							// die tatsaechliche anzahl an bytes
	uint32_t count;								// zaehler fuer cluster in der fat
	uint32_t fatSz;								// zuerst groeße der fat in sektoren, dann letzter sektor der fat in clustern
	uint16_t tmp;									// hilfsvariable zum zaehlen
	void *vsector;

	// bestimmen des 1. sektors der 1. partition (ist nach der initialisierung nicht mehr bekannt...)
	fat_loadSector(0);
	vsector=&fat.sector[454];
	fatSz=*(uint32_t*)vsector;
	if(fatSz!=0x6964654d){
		fat_loadSector(fatSz);
		}

	// anzahl sektoren bestimmen die von einer fat belegt werden
	if(fat.fatType==32){
		vsector=&fat.sector[36];
		fatSz=(*(uint32_t *)vsector)-1;
		fatSz*=128;										// multipliziert auf cluster/sektor in der fat, also ein sektor der fat beinhaltet 128 cluster nummern
		}
	else{
		vsector=&fat.sector[22];
		fatSz=(*(uint16_t*)vsector)-1;
		fatSz*=256;										// multipliziert auf cluster/sektor in der fat, also ein sektor der fat beinhaltet 256 cluster nummern
		}

	// variablen initialisieren
	tmp=0;
	bytes=0;
	count=0;

	// zaehlen der freien cluster in der fat, mit hilfsvariable um nicht eine 64 bit variable pro freien cluster hochzaehlen zu muessen
	do{
		if(0==fat_getNextCluster(count)) tmp++;
		if(tmp==254){
			bytes+=254;
			tmp=0;
		}
		count++;
	}while(count<fatSz);
	bytes+=tmp;

	// multiplizieren um auf bytes zu kommen
	return (bytes*fat.secPerClust)<<9;   // 2^9 = 512
}
#endif



