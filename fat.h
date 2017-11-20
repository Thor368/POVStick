/*
 * 	Doku, siehe http://www.mikrocontroller.net/articles/AVR_FAT32
 *  Neuste Version: http://www.mikrocontroller.net/svnbrowser/avr-fat32/
 *	Autor: Daniel R.
 */



#ifndef _FAT_H
  #define _FAT_H

	// #######################################################################################################################
	// "daten" ketten siehe doku...
	// 1. fat_getFreeRowOfCluster -> fat_getFreeRowOfDir -> fat_makeRowDataEntry -> fat_makeFileEntry -> fat_writeSector  "eintrag gemacht !!"
	// 2. fat_loadSector -> fat_loadRowOfSector -> fat_loadFileDataFromCluster -> fat_loadFileDataFromDir (-> fat_cd)   "daten chain"

	// #######################################################################################################################
	// funktionen

  extern uint32_t fat_clustToSec(uint32_t);				// rechnet cluster zu 1. sektor des clusters um
  extern uint32_t fat_secToClust(uint32_t sec);			// rechnet sektor zu cluster um!
  extern uint32_t fat_getNextCluster(uint32_t oneCluster);// fat auf naechsten, verketteten cluster durchsuchen
  extern uint64_t fat_getFreeBytes(void);						// berechnet den freien platz der karte in bytes!
  extern uint8_t fat_writeSector(uint32_t sec);				// schreibt sektor auf karte
  extern uint8_t fat_loadSector(uint32_t sec);				// laed Uebergebenen absoluten sektor
  extern uint8_t fat_loadFileDataFromDir( uint8_t name []);	// durchsucht das aktuelle directory
  extern uint8_t fat_loadFatData(void);								// laed fat daten
  extern void 	fat_loadRowOfSector(uint16_t row);			// laed reihe des geladen sektors auf struct:file
  extern void 	fat_setCluster( uint32_t cluster, uint32_t content); 	// setzt cluster inhalt in der fat
  extern void 	fat_delClusterChain(uint32_t startCluster);			// loescht cluster-chain in der fat
  extern void 	fat_makeFileEntry( uint8_t name [],uint8_t attrib); // macht einen datei/ordner eintrag
  extern void 	fat_getFreeClustersInRow(uint32_t offsetCluster);		// sucht zusammenhaengende freie cluster aus der fat
  extern void 	fat_getFatChainClustersInRow( uint32_t offsetCluster);	// sucht fat-chain cluster die zusammenhaengen
  extern void 	fat_setClusterChain(uint32_t startCluster, uint32_t endCluster); // verkettet cluster zu einer cluster-chain
  void 			fat_makeSfnDataEntry(uint8_t name [],uint8_t attrib,uint32_t cluster,uint32_t length);

  // #######################################################################################################################
  // variablen



#endif


 



