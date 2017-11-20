/*
 * 	Doku, siehe http://www.mikrocontroller.net/articles/AVR_FAT32
 *  Neuste Version: http://www.mikrocontroller.net/svnbrowser/avr-fat32/
 *	Autor: Daniel R.
 */

#include "mmc_config.h"
#include "mmc.h"
#include "fat.h"
#include "file.h"

struct File_t file;			// wichtige dateibezogene daten/variablen

#if (MMC_MULTI_BLOCK==TRUE && MMC_OVER_WRITE==FALSE)
	uint8_t  multiblock_flag;
#endif


//*******************************************************************************************************************************
// funktionsprototypen die nur in dieser datei benutzt werden !
#if (MMC_LS==TRUE)
	static void lsRowsOfClust (fptr_t uputs_ptr, uint32_t start_sec);	// zeigt reihen eines clusters an, ab start_sec, muss zeiger auf eine ausgabe funktion überbeben bekommen
#endif
//*******************************************************************************************************************************


#if (MMC_FILE_EXSISTS==TRUE)
// *******************************************************************************************************************************
// prueft ob es die datei im aktuellen verzeichnis gibt.
// ffopen wuerde die datei direkt anlegen, falls es sie noch nicht gibt!
// *******************************************************************************************************************************
uint8_t ffileExsists ( uint8_t name[]){
	return fat_loadFileDataFromDir(name);
}
#endif

//*******************************************************************************************************************************
// 2 moeglichkeiten beim oeffnen, datei existiert(return MMC_FILE_OPENED) oder muss angelegt werden(return MMC_FILE_CREATED)
// zuerst wird geprueft ob es die datei im verzeichniss gibt. danach wird entschieden, ob die datei geoeffnet wird oder angelegt.
// -beim oeffnen werden die bekannten cluster gesucht maximal MAX_CLUSTERS_IN_ROW in reihe. dann wird der 1. sektor der datei auf
// den puffer fat.sector geladen. jetzt kann man ffread lesen...
// -beim anlegen werden freie cluster gesucht, maximal MAX_CLUSTERS_IN_ROW in reihe. dann wird das struct file gefuellt.
// danach wird der dateieintrag gemacht(auf karte). dort wird auch geprueft ob genügend platz im aktuellen verzeichniss existiert.
// moeglicherweise wird der 1. cluster der datei nochmal geaendert. jetzt ist der erste frei sektor bekannt und es kann geschrieben werden.
//*******************************************************************************************************************************
uint8_t ffopen( uint8_t name[], uint8_t rw_flag){

	uint8_t file_flag = fat_loadFileDataFromDir(name);	//pruefung ob datei vorhanden. wenn vorhanden, laden der daten auf das file struct.

	if( file_flag==TRUE && rw_flag == 'r' ){					/** datei existiert, alles vorbereiten zum lesen **/
		fat_getFatChainClustersInRow( file.firstCluster );		// verkettete cluster aus der fat-chain suchen.
		file.name = name;
		#if (MMC_MULTI_BLOCK==TRUE && MMC_OVER_WRITE==FALSE)
			fat.currentSectorNr = fat.startSectors;
			multiblock_flag = TRUE;
			mmc_multi_block_start_read (fat.startSectors);
			mmc_multi_block_read_sector (fat.sector);
		#else
			fat_loadSector( chain.startSectors );				// laed die ersten 512 bytes der datei auf puffer:sector.
		#endif
		return MMC_FILE_OPENED;
	}

	#if (MMC_WRITE==TRUE)										// anlegen ist schreiben !
	if( file_flag==FALSE && rw_flag == 'c' ){					/** datei existiert nicht, also anlegen !	(nur wenn schreiben option an ist)**/
		file.name = name;										// pointer "sichern" ;)
		fat_getFreeClustersInRow( 2 ); 							// freie cluster aus der fat suchen. wird eigentlich erst bei fat_getFreeRowsOfDir benoetigt und spaeter bei ffwrite !
		fat_makeFileEntry(name,0x20);							// DATEI ANLEGEN auf karte
		#if (MMC_MULTI_BLOCK==TRUE && MMC_OVER_WRITE==FALSE)
			multiblock_flag = FALSE;
			mmc_multi_block_start_write(fat.currentSectorNr);
		#endif
		fat.bufferDirty = TRUE;									// puffer dirty weil geschrieben und noch nicht auf karte.
		return MMC_FILE_CREATED;
	}		
	#endif

   	return MMC_FILE_ERROR;
}


//*******************************************************************************************************************************
// schliesst die datei operation ab. eigentlich nur noetig wenn geschrieben/ueberschrieben wurde. es gibt 2 moeglichkeiten :
// 1. die datei wird geschlossen und es wurde ueber die alte datei länge hinaus geschrieben.
// 2. die datei wird geschlossen und man war innerhalb der datei groesse, dann muss nur der aktuelle sektor geschrieben werden.
// der erste fall ist komplizierter, weil ermittelt werden muss wie viele sektoren neu beschrieben wurden um diese zu verketten
// und die neue datei laenge muss ermitt weden. abschließend wird entweder (fall 2) nur der aktuelle sektor geschrieben, oder
// der aktuallisierte datei eintrag und die cluster (diese werden verkettet, siehe fileUpdate() ).
//*******************************************************************************************************************************
uint8_t ffclose(){

	#if (MMC_MULTI_BLOCK==TRUE && MMC_OVER_WRITE==FALSE)
		if(multiblock_flag == TRUE){
			mmc_multi_block_stop_read(); 	// stoppen von multiblock aktion
		}
		else{
			mmc_multi_block_stop_write(); 	// stoppen von multiblock aktion
		}
	#endif

	#if (MMC_WRITE==TRUE) 	/** 2 moeglichkeiten beim schliessen !!	(lesend spielt keine rolle, nichts muss geupdatet werden) **/
		fflushFileData();
	#endif

	file.cntOfBytes = 0;
	file.seek = 0;

	return TRUE;
}


#if (MMC_WRITE==TRUE)
// *******************************************************************************************************************************
// füllt grade bearbeiteten  sektor auf, verkette die bis jetzt benutzten cluster. macht datei eintrag update.
// *******************************************************************************************************************************
void fflushFileData(void){

	uint32_t new_length;
	uint32_t save_currentSectorNr;
	uint16_t 	count;
	uint16_t 	row;

	#if (MMC_ENDIANNESS_LITTLE == TRUE)
		void 				*psector;
	#else
		uint8_t 		*psector;
	#endif	
	
	/** 2 moeglichkeiten ab hier **/
	/** 1.) es wurde ueber die alte datei groesse hinaus geschrieben **/
	if( file.length < (file.seek + file.cntOfBytes) )	{

		new_length = file.seek + file.cntOfBytes;
		save_currentSectorNr = file.currentSectorNr;

		// wenn fat.cntOfBytes == 0 ist der sektor gerade vorher schon geschrieben worden oder es ist noch nichts hinein geschrieben worden. dann wert so setzten, dass die schleife uebersprungen wird
		count = (file.cntOfBytes==0) ? 512:file.cntOfBytes;
		while( count < 512 ){						// sektor ist beschrieben worden, daher rest mit 00 fuellen
			fat.sector[count] = 0x00; 			
			count += 1;
			fat.bufferDirty = TRUE;
		}

		// cluster chain verketten.
		if(file.cntOfBytes == 0){								// gerade genau irgend einen sektor voll geschrieben.file.currentSectorNr zeigt jetzt auf den neu zu beschreibenden.
			if( file.currentSectorNr-1 >= chain.startSectors ){
				fat_setClusterChain(fat_secToClust(chain.startSectors),fat_secToClust(file.currentSectorNr-1));	// verketten der geschriebenen cluster
			}
		}
		else fat_setClusterChain( fat_secToClust(chain.startSectors),fat_secToClust(file.currentSectorNr) );


		// dateigroesse updaten
		fat_loadSector(file.entrySector);						// laden des sektors mit dem dateieintrag zwecks update :)
		row = file.row;
		row <<=5;  												// um von reihen nummer auf byte zahl zu kommen
		psector =& fat.sector[row+28];

		#if (MMC_ENDIANNESS_LITTLE == TRUE)
			*(uint32_t*)psector = new_length;
		#else
			*psector++ = new_length;
			*psector++ = new_length >> 8;
			*psector++ = new_length >> 16;
			*psector   = new_length >> 24;
		#endif

		file.length = new_length;								// die richtige groesse sichern, evtl beim suchen der datei mit mist vollgeschrieben
		chain.startSectors = save_currentSectorNr;				// da die sektoren bis zu save_currentSectorNr schon verkettet wurden, reicht es jetzt die kette ab da zu betrachten.

		// daruer sorgen, dass der sektor mit dem geaenderten datei eintrag auf die karte geschrieben wird!
		fat.bufferDirty = TRUE;
		fat_loadSector( save_currentSectorNr );		
	}

	/** 2.) es wurde nicht ueber die alte datei groesse hinaus geschrieben **/
	else{
		fat_writeSector( file.currentSectorNr );		// einfach den gerade bearbeiteten sektor schreiben reicht, alle dateiinfos stimmen weil nicht ueber die alte groesse hinaus geschrieben
	}
	
}
#endif

#if (MMC_SEEK==TRUE)
// *******************************************************************************************************************************
// offset byte wird uebergeben. es wird durch die sektoren der datei gespult (gerechnet), bis der sektor mit dem offset byte erreicht
// ist, dann wird der sektor geladen und der zaehler fuer die bytes eines sektors gesetzt. wenn das byte nicht in den sektoren ist,
// die "vorgesucht" wurden, muessen noch weitere sektoren der datei gesucht werden (sec > fat.endSectors).
// *******************************************************************************************************************************
void ffseek(uint32_t offset){

	uint32_t sectors;							// zum durchgehen durch die sektoren der datei

	#if (MMC_MULTI_BLOCK==TRUE && MMC_OVER_WRITE==FALSE)
		uint8_t seek_end;
		seek_end = FALSE;

		if(offset == file.length) {						// zum ende spulen, kann dann nur schreiben sein...
			seek_end = TRUE;
		}
		if(multiblock_flag == FALSE){
			mmc_multi_block_stop_write();
		}
		else {
			mmc_multi_block_stop_read();
		}
	#endif

	#if (MMC_OVER_WRITE==TRUE && MMC_WRITE==TRUE)		// nur wenn ueberschreiben an ist.
		fflushFileData();								// fat verketten und datei update auf der karte !
	#endif

	fat_getFatChainClustersInRow(file.firstCluster);	// suchen von anfang der cluster chain aus, das geht nicht vom ende zum anfang oder so, weil es eine einfach verkettete liste ist !
	sectors = chain.startSectors;							// sektor variable zum durchgehen durch die sektoren
	file.seek = 0;										// weil auch von anfang an der chain gesucht wird mit 0 initialisiert

	// suchen des sektors in dem offset ist
	while( offset >= 512 ){
		sectors += 1;									// da byte nicht in diesem sektor ist, muss hochgezählt werden
		offset -= 512;									// ein sektor weniger in dem das byte sein kann
		file.seek += 512;								// file.seek update, damit bei ffclose() die richtige leange herauskommt
		chain.cntSecs -= 1;								// damit die zu lesenden/schreibenden sektoren in einer reihe stimmen
		if ( chain.cntSecs == 0 ){						// es muessen mehr sektoren der datei gesucht werden. sektoren in einer reihe durchgegangen
			fat_getFatChainClustersInRow(fat_getNextCluster( chain.lastCluster ) );	// suchen von weiteren sektoren der datei in einer reihe
			sectors = chain.startSectors;						// setzen des 1. sektors der neu geladenen, zum weitersuchen !
		}
	}

	fat_loadSector(sectors);  								// sektor mit offset byte laden
	file.cntOfBytes = offset;								// setzen des lese zaehlers

	#if (MMC_MULTI_BLOCK==TRUE && MMC_OVER_WRITE==FALSE)
		if(seek_end == TRUE) {
			mmc_multi_block_start_write(sectors);			// starten von multiblock aktion
			fat.bufferDirty = TRUE;
		}
		else {
			mmc_multi_block_start_read(sectors);			// starten von multiblock aktion
		}
	#endif
}
#endif


#if (MMC_CD==TRUE)
// *******************************************************************************************************************************
// wechselt verzeichniss. start immer im root Dir.
// es MUSS in das direktory gewechselt werden, in dem die datei zum lesen/schreiben ist !
// *******************************************************************************************************************************
uint8_t ffcd( uint8_t name[]){

	if(name[0]==0){									// ZUM ROOTDIR FAT16/32
		fat.dir=0;									// root dir
		return TRUE;
	}

	if( TRUE == fat_loadFileDataFromDir(name) ){	// NICHT ROOTDIR	(fat16/32)
		fat.dir = file.firstCluster;				// zeigt auf 1.cluster des dir	(fat16/32)
		return TRUE;
	}

	return FALSE;									// dir nicht gewechselt (nicht da?) !!
}

//*******************************************************************************************************************************
// wechselt in das parent verzeichniss (ein verzeichniss zurück !)
// die variable fat.dir enthält den start cluster des direktory in dem man sich grade befindet, anhand diesem,
// kann der "." bzw ".." eintrag im ersten sektor des direktory ausgelesen und das parent direktory bestimmt werden.
//*******************************************************************************************************************************
uint8_t ffcdLower(void){

  if( fat.dir == 0 )return FALSE;			// im root dir, man kann nicht hoeher !

  fat_loadSector(fat_clustToSec(fat.dir));	// laed 1. sektor des aktuellen direktory.
  fat_loadRowOfSector(1);					// ".." eintrag (parent dir) ist 0 wenn parent == root
  fat.dir=file.firstCluster;				// dir setzen

  return TRUE;
}
#endif


#if (MMC_LS==TRUE)
// *******************************************************************************************************************************
// zeigt reihen eines clusters an, wird fuer ffls benoetigt !
// es wird ab dem start sektor start_sec, der dazugehoerige cluster angezeigt. geprueft wird ob es ein richtiger
// eintrag in der reihe ist (nicht geloescht, nicht frei usw). die sektoren des clusters werden nachgeladen.
// die dateien werden mit namen und datei groesse angezeigt.
// *******************************************************************************************************************************
static void lsRowsOfClust (fptr_t uputs_ptr,uint32_t start_sec){

	uint16_t row;				// reihen
	uint8_t sec;				// sektoren
	uint8_t tmp[12];			// tmp string zur umwandlung
	uint8_t i;				// um dateinamen zu lesen

	sec=0;
	do{
		fat_loadSector( start_sec + sec );	// sektoren des clusters laden
		for( row=0; row<512; row+=32 ){		// geht durch reihen des sektors

			if( (fat.sector[row+11]==0x20||fat.sector[row+11]==0x10) && (fat.sector[row]!=0xE5 && fat.sector[row]!=0x00) ){
				
				// namen extrahieren und anzeigen
				for( i=0 ; i<11 ; i++ ){
					tmp[i]=fat.sector[row+i];
				}
				tmp[i]='\0';
				uputs_ptr(tmp);

				// reihe auf file stuct laden um file.length zu bekommen. koverieren und anzeigen...
				fat_loadRowOfSector(row);		
				uputs_ptr((uint8_t*)" ");
				ltostr(file.length,(char*)tmp,12,10);
				uputs_ptr(tmp);
				uputs_ptr((uint8_t*)"\n");
			}
		}
	}while( ++sec < fat.secPerClust );
}

// *******************************************************************************************************************************
// zeigt inhalt eines direktory an.
// unterscheidung ob man sich im rootDir befindet noetig, weil bei fat16 im root dir eine bestimmt anzahl sektoren durchsucht
// werden muessen und bei fat32 ab einem start cluster ! ruft lsRowsOfClust auf um cluster/sektoren anzuzeigen.
// *******************************************************************************************************************************
void ffls(fptr_t uputs_ptr){

	uint8_t 		sectors;	// variable um durch sektoren zu zaehlen
	uint32_t 	clusters;	// variable um durch cluster des verzeichnisses zu gehen

	// bestimmen ab welchem cluster eintraege angezeigt werden sollen, bzw in welchem ordner man sich befindet
	clusters = (fat.dir==0) ? fat.rootDir:fat.dir;

	// root-dir fat16 nach eintrag durchsuchen. es bleiben noch 3 moeglichkeiten: nicht root-dir fat16, nicht root-dir fat32 und root-dir fat32
	if(fat.dir==0 && fat.fatType==16){
		sectors = 0;
		do{
			// root-dir eintraege anzeigen
			lsRowsOfClust( uputs_ptr, clusters + sectors );
			sectors += fat.secPerClust;
		}while( sectors < (uint8_t)32 );
	}

	// root-dir fat32 oder nicht root-dir fat32/16, nach eintrag durchsuchen
	else {
		// durch cluster des verzeichnisses gehen und eintraege anzeigen
		while(!((clusters>=0x0ffffff8&&fat.fatType==32)||(clusters>=0xfff8&&fat.fatType==16))){		// prueft ob weitere sektoren zum lesen da sind (fat32||fat16)
			lsRowsOfClust( uputs_ptr, fat_clustToSec(clusters) );									// zeigt reihen des clusters an
			clusters = fat_getNextCluster(clusters);												// liest naechsten cluster des dir-eintrags (unterverzeichniss groeßer 16 einträge)
		}
	}

}
#endif



#if (MMC_WRITE==TRUE && MMC_MKDIR==TRUE)
// *******************************************************************************************************************************
// erstellt einen dir eintrag im aktuellen verzeichnis.
// prueft ob es den den dir-namen schon gibt, dann wird nichts angelegt.
// wenn ok, dann wird ein freier cluster gesucht, als ende markiert, der eintrag ins dir geschrieben.
// dann wird der cluster des dirs aufbereitet. der erste sektor des clusters enthaelt den "." und ".." eintrag.
// der "." hat den 1. cluster des eigenen dirs. der ".." eintrag ist der 1. cluster des parent dirs.
// ein dir wird immer mit 0x00 initialisiert ! also alle eintraege der sektoren des clusters ( bis auf . und .. einträge)!
// *******************************************************************************************************************************
void ffmkdir( uint8_t name[]){

	uint8_t i;								// variable zum zaehlen der sektoren eines clusters.
	uint16_t j;								// variable zum zaehlen der sektor bytes auf dem puffer fat.sector.
	uint8_t l;

	if(TRUE==fat_loadFileDataFromDir(name))			// prueft ob dirname im dir schon vorhanden, wenn ja, abbruch !
		return ;

	// cluster in fat setzen, und ordner eintrg im aktuellen verzeichniss machen.
	fat_getFreeClustersInRow(2);									// holt neue freie cluster, ab 2 ...
	fat_setCluster(fat_secToClust(chain.startSectors),0x0fffffff);	// fat16/32 cluster chain ende setzen.	(neuer ordner in fat)
	file.firstCluster = fat_secToClust(chain.startSectors);			// damit fat_makeFileEntry den cluster richtig setzen kann

	fat_makeFileEntry(name,0x10); 				// legt ordner im partent verzeichnis an.

	// aufbereiten des puffers
	j=511;
	do{
		fat.sector[j]=0x00;						//schreibt puffer fat.sector voll mit 0x00==leer
	}while(j--);

	// aufbereiten des clusters
	for(i=1;i<fat.secPerClust;i++){				// ein dir cluster muss mit 0x00 initialisiert werden !
		fat_writeSector(chain.startSectors+i);	// loeschen des cluster (ueberschreibt mit 0x00), wichtig bei ffls!
	} 

	l = 0;
	do{
		fat.sector[l] = ' ';
		fat.sector[l+32] = ' ';
	}while(++l<11);

	fat.sector[l]= 0x10;
	fat.sector[l+32] = 0x10;

	fat.sector[0] = '.';
	fat.sector[32] = '.';
	fat.sector[33] = '.';

	#if (MMC_ENDIANNESS_LITTLE==TRUE)
		void *vsector =& fat.sector[20];

		*(uint16_t*)vsector=(file.firstCluster&0xffff0000)>>16;		// hi word	von cluster (20,2)
		vsector += 6;

		*(uint16_t*)vsector=(file.firstCluster&0x0000ffff);			// low word von cluster (26,2)

		vsector += 26;

		*(uint16_t*)vsector=(fat.dir&0xffff0000)>>16;				// hi word	von cluster (20,2)
		vsector+=6;

		*(uint16_t*)vsector=(fat.dir&0x0000ffff);					// low word von cluster (26,2)

	#else
		uint8_t *psector =& fat.sector[20];

		*psector++=(file.firstCluster&0x00ff0000)>>16;				// hi word	von cluster (20,2)
		*psector=(file.firstCluster&0xff000000)>>24;
		psector += 5;

		*psector++=file.firstCluster&0x000000ff;					// low word von cluster (26,2)
		*psector=(file.firstCluster&0x0000ff00)>>8;

		psector += 25;

		*psector++=(fat.dir&0x00ff0000)>>16;						// hi word	von cluster (20,2)
		*psector=(fat.dir&0xff000000)>>24;
		psector += 5;

		*psector++=fat.dir&0x000000ff;								// low word von cluster (26,2)
		*psector=(fat.dir&0x0000ff00)>>8;


	#endif

	// "." und ".." eintrag auf karte schreiben
	mmc_write_sector(chain.startSectors,fat.sector);
}
#endif



#if (MMC_WRITE==TRUE && MMC_RM==TRUE)
//*******************************************************************************************************************************
// loescht datei/ordner aus aktuellem verzeichniss, wenn es die/den datei/ordner gibt.
// loescht dateien und ordner rekursiv !
//*******************************************************************************************************************************
uint8_t ffrm( uint8_t name[] ){

	#if(MMC_RM_FILES_ONLY==FALSE)
		uint32_t parent;		// vaterverzeichnis cluster
		uint32_t own;			// cluster des eigenen verzeichnis
		uint32_t clustsOfDir;	// um durch die cluster eines dirs zu gehen
		uint8_t cntSecOfClust;	// um durch die sektoren eines clusters zu gehen
	#endif

	uint16_t row;				// um durch die reihen eines sektors zu gehen. springt immer auf das anfangsbyte eines eintrags
	uint8_t fileAttrib;			// zum sichern des dateiattributs

	// datei/ordner nicht vorhanden, dann nicht loschen...
	if(FALSE==fat_loadFileDataFromDir(name)){
		return FALSE;
	}

	// reihe zu byte offset umrechnen, attribut sichern. anhand des attributs wird spaeter weiter entschieden
	row = file.row;
	row = row<<5;
	fileAttrib = fat.sector[row+11];

	#if(MMC_RM_FILES_ONLY==TRUE)
		// keine datei, also abbruch !
		if(fileAttrib!=0x20) return FALSE;
	#endif

	//////// ob ordner oder datei, der sfn und lfn muss geloescht werden!
	fat.bufferDirty = TRUE;						// damit beim laden der geaenderte puffer geschrieben wird
	do{
		fat.sector[row] = 0xE5;
		if( row == 0 ){							// eintraege gehen im vorherigen sektor weiter
			fat_loadSector(fat.lastSector);		// der davor ist noch bekannt. selbst wenn der aus dem cluster davor stammt.
			fat.bufferDirty = TRUE;
			row = 512;							// da nochmal row-=32 gemacht wird, kommt man so auf den anfang der letzten zeile
		}
		row -= 32;
	}while(  fat.sector[row+11]==0x0f && (fat.sector[row]&0x40) != 0x40);		// geht durch alle reihen bis neuer eintrag erkannt wird...

	fat.sector[row] = 0xE5;

	#if(MMC_RM_FILES_ONLY==TRUE)
		fat_delClusterChain(file.firstCluster);	// loescht die zu der datei gehoerige cluster-chain aus der fat.
	#else
		//// ist datei. dann nur noch cluster-chain loeschen und fertig.
		if( fileAttrib == 0x20 ){
			fat_delClusterChain(file.firstCluster);	// loescht die zu der datei gehoerige cluster-chain aus der fat.
		}
		//// ist ordner. jetzt rekursiv durch alle zweige/aeste des baums, den dieser ordner evtl. aufspannt, und alles loeschen!
		else{
			clustsOfDir = file.firstCluster;
			// jetzt eigentlichen ordnerinhalt rekursiv loeschen!
			do{
				do{
					cntSecOfClust=0;
					do{
						fat_loadSector( fat_clustToSec(clustsOfDir) + cntSecOfClust );		// in ordner wechseln / neuen cluster laden zum bearbeiten
						row=0;
						do{
							// nach einem leeren eintrag kommt nix mehr. cntSecOfClust wird auch gesetzt, damit man auch durch die naechste schleife faellt!
							if(fat.sector[row]==0x00){
								cntSecOfClust = fat.secPerClust;
								break;
							}

							// sfn eintrag erkannt. eintrag geloescht markieren, cluster chain loeschen, diesen sektor neu laden!
							if( fat.sector[row]!=0xE5 && fat.sector[row+11]!=0x10 && fat.sector[row+11]!=0x0F ){
								fat_loadRowOfSector(row);
								fat.sector[row] = 0xE5;		// hier evtl. nur pruefen ob file.firstCluster nicht schon in der fat geloescht ist...
								fat.bufferDirty = TRUE;
								fat_delClusterChain(file.firstCluster);
								fat_loadSector( fat_clustToSec(clustsOfDir) + cntSecOfClust );	// aktuellen sektor wieder laden...wurde bei fat_delClusterChain geaendert
							}

							// punkt eintrag erkannt, own und parent sichern!
							if( fat.sector[row]=='.' && row==0 && fat.sector[row+11]==0x10){
								fat_loadRowOfSector(row);
								own = file.firstCluster;
								fat_loadRowOfSector(row+32);
								parent = file.firstCluster;
							}

							// ordner erkannt. jetzt ordner eintrag loeschen, hinein wechseln und alles von vorne...
							if(  fat.sector[row]!='.' && fat.sector[row]!=0xE5 && fat.sector[row+11]==0x10){
								fat_loadRowOfSector(row);
								fat.sector[row] = 0xE5;
								fat.bufferDirty = TRUE;
								clustsOfDir = file.firstCluster;
								fat_loadSector( fat_clustToSec(clustsOfDir) );	// hier wird in das dir gewechselt!
								row = 0;
								cntSecOfClust = 0;
								continue;
							}

							row += 32;
							}while( row < 512 );
					}while( ++cntSecOfClust < fat.secPerClust );
					clustsOfDir = fat_getNextCluster(clustsOfDir);		// holen des folgeclusters um durch die chain zu gehen
				}while( !((clustsOfDir>=0x0ffffff8 && fat.fatType==32) || (clustsOfDir==0xfff8 && fat.fatType==16)) );		// geht durch cluster des dirs.

				// hier ist man am ende eines astes. also ast loeschen und zuruck zum stamm :)
				fat_delClusterChain(own);

				// stamm ist ein weiterer ast wenn parent != fat.dir. ast wird oben geladen und weiter verarbeitet
				clustsOfDir = parent;

			}while(parent!=fat.dir);
		}
	#endif
	return TRUE;				// alles ok, datei/ordner geloescht !
}
#endif


// *******************************************************************************************************************************
// liest 512 bytes aus dem puffer fat.sector. dann werden neue 512 bytes der datei geladen, sind nicht genuegend verkettete
// sektoren in einer reihe bekannt, wird in der fat nachgeschaut. dann werden weiter bekannte nachgeladen...
// *******************************************************************************************************************************
uint8_t ffread(void){

	uint32_t nc;

	if( file.cntOfBytes == 512 ){							// EINEN SEKTOR GLESEN (ab hier 2 moeglichkeiten !)

		chain.cntSecs-=1;										// anzahl der sektoren am stück werden weniger, bis zu 0 dann müssen neue gesucht werden...

		if( 0==chain.cntSecs ){		 						//1.) noetig mehr sektoren der chain zu laden (mit ein bisschen glück nur alle 512*MAX_CLUSTERS_IN_ROW bytes)

			#if (MMC_MULTI_BLOCK==TRUE && MMC_OVER_WRITE==FALSE)
				mmc_multi_block_stop_read ();					// stoppen von multiblock aktion
			#endif

			nc = fat_secToClust( file.currentSectorNr );		// umrechnen der aktuellen sektor position in cluster
			nc = fat_getNextCluster(nc);					// in der fat nach neuem ketten anfang suchen
			fat_getFatChainClustersInRow(nc);				// zusammenhängende cluster/sektoren der datei suchen
			file.currentSectorNr = chain.startSectors - 1;		// hier muss erniedrigt werden, da nach dem if immer ++ gemacht wird

			#if (MMC_MULTI_BLOCK==TRUE && MMC_OVER_WRITE==FALSE)
				mmc_multi_block_start_read (fat.startSectors);	// starten von multiblock lesen ab dem neu gesuchten start sektor der kette.
			#endif
		}

		file.cntOfBytes = 0;

		#if (MMC_MULTI_BLOCK==TRUE && MMC_OVER_WRITE==FALSE)
			fat.currentSectorNr+=1;
			mmc_multi_block_read_sector (fat.sector);			//2.) bekannte sektoren reichen noch, also einfach nachladen..
		#else
			fat_loadSector(file.currentSectorNr+1);				//2.) die bekannten in einer reihe reichen noch.(nur alle 512 bytes)
		#endif
	}
	return fat.sector[file.cntOfBytes++];
}



#if (MMC_OVER_WRITE==FALSE && MMC_WRITE==TRUE)			// nicht ueberschreibende write funktion
//*******************************************************************************************************************************
// schreibt 512 byte bloecke auf den puffer fat.sector. dann wird dieser auf die karte geschrieben. wenn genuegend feie
// sektoren zum beschreiben bekannt sind(datenmenge zu gross), muss nicht in der fat nachgeschaut werden. sollten nicht genuegend
// zusammenhaengende sektoren bekannt sein, werden die alten verkettet und neue gesucht. es ist noetig sich den letzten bekannten
// einer kette zu merken -> file.lastCluster, um auch nicht zusammenhaengende cluster verketten zu koennen (fat_setClusterChain macht das)!
//*******************************************************************************************************************************
void ffwrite( uint8_t c){

	fat.sector[file.cntOfBytes++] = c;							// schreiben des chars auf den puffer sector und zaehler erhoehen (pre-increment)

	if( file.cntOfBytes == 512 ){ 							/** SEKTOR VOLL ( 2 moeglichkeiten ab hier !) **/
		file.seek += 512;											// position in der datei erhoehen, weil grade 512 bytes geschrieben
		file.cntOfBytes = 0;	  										// ruecksetzen des sektor byte zaehlers
		chain.cntSecs -= 1;											// noch maximal cntSecs sekoren zum beschreiben bekannt...

		#if (MMC_MULTI_BLOCK==TRUE)							/** 1.) genuegend leere zum beschreiben bekannt **/
			mmc_multi_block_write_sector(fat.sector);				// gefuellten sektor schreiben
		#else
			mmc_write_sector(file.currentSectorNr,fat.sector);		// gefuellten sektor schreiben
		#endif

		file.currentSectorNr += 1;									// naechste sektor nummer zum beschreiben.

		if( chain.cntSecs == 0 ){ 							/** 2.) es ist noetig, neue freie zu suchen und die alten zu verketten (mit ein bischen glueck nur alle 512*MAX_CLUSTERS_IN_ROW bytes) **/
			#if (MMC_MULTI_BLOCK==TRUE)
				mmc_multi_block_stop_write();
			#endif
			fat.bufferDirty = FALSE;								// sonst wuerde das suchen von clustern wieder eine schreiboperation ausloesen...
			fat_setClusterChain( fat_secToClust(chain.startSectors) , fat_secToClust(file.currentSectorNr-1) );	// verketten der beschriebenen
			fat_getFreeClustersInRow( chain.lastCluster );				// suchen von leeren sektoren in einer reihe.
			file.currentSectorNr = chain.startSectors;					// setzen des 1. sektors der neuen reihe zum schreiben.
			fat.bufferDirty = TRUE;
			#if (MMC_MULTI_BLOCK==TRUE)
				mmc_multi_block_start_write(fat.currentSectorNr);
			#endif
		}
	}
}
#endif

#if (MMC_OVER_WRITE==TRUE && MMC_WRITE==TRUE)  			// ueberschreibende write funktion
//*******************************************************************************************************************************
// schreibt 512 byte bloecke auf den puffer fat.sector. dann wird dieser auf die karte geschrieben. wenn genuegend feie
// sektoren zum beschreiben bekannt sind, muss nicht in der fat nachgeschaut werden. sollten nicht genuegend zusammenhaengende
// sektoren bekannt sein(datenmenge zu gross), werden die alten verkettet und neue gesucht. es ist noetig sich den letzten bekannten einer
// kette zu merken -> file.lastCluster, um auch nicht zusammenhaengende cluster verketten zu koennen (fat_setClusterChain macht das)!
// es ist beim ueberschreiben noetig, die schon beschriebenen sektoren der datei zu laden, damit man die richtigen daten
// hat. das ist bloed, weil so ein daten overhead von 50% entsteht. da lesen aber schneller als schreiben geht, verliert man nicht 50% an geschwindigkeit.
//*******************************************************************************************************************************
void ffwrite( uint8_t c){

	fat.sector[ file.cntOfBytes++ ]=c;							// schreiben des chars auf den puffer sector und zaehler erhoehen (pre-increment)

	if( file.cntOfBytes==512 ){									/** SEKTOR VOLL ( 3 moeglichkeiten ab hier !) **/

		file.cntOfBytes = 0;	  									// ruecksetzen des sektor byte zaehlers.
		file.seek += 512;											// position in der datei erhoehen, weil grade 512 bytes geschrieben.
		mmc_write_sector( file.currentSectorNr,fat.sector );	/** 1.) vollen sektor auf karte schreiben, es sind noch freie sektoren bekannt**/
		file.currentSectorNr +=1;									// naechsten sektor zum beschreiben.
		chain.cntSecs -=1;											// einen freien sektor zum beschreiben weniger.

		if( chain.cntSecs==0 ){										// ende der bekannten in einer reihe erreicht (freie oder verkettete)
			if( file.seek > file.length ){						/** 2.) ausserhalb der datei, jetzt ist es noetig die beschriebenen cluster zu verketten und neue freie zu suchen	**/
				fat.bufferDirty = FALSE;							// damit nicht durch z.b. fat_getNextCluster nochmal dieser sektor gescchrieben wird, siehe fat_loadSector
				fat_setClusterChain( fat_secToClust(chain.startSectors) , fat_secToClust(file.currentSectorNr-1) );	// verketten der beschriebenen.
				fat_getFreeClustersInRow( chain.lastCluster );		// neue leere sektoren benoetigt, also suchen.
				file.currentSectorNr = chain.startSectors;				// setzen des 1. sektors der neuen reihe zum schreiben.
				fat.bufferDirty = TRUE;
			}
			else {												/** 3.) noch innerhalb der datei, aber es muessen neue verkettete cluster gesucht werden, zum ueberschreiben **/
				fat_getFatChainClustersInRow( fat_getNextCluster(chain.lastCluster) );		// noch innerhalb der datei, deshlab verkettete suchen.
				file.currentSectorNr = chain.startSectors;				// setzen des 1. sektors der neuen reihe zum schreiben.
			}
		}

		if( file.seek <= file.length ){
			mmc_read_sector(file.currentSectorNr,fat.sector);		// wegen ueberschreiben, muss der zu beschreibende sektor geladen werden (zustand 3)...
		}
	}
}
#endif


#if (MMC_WRITE_STRING==TRUE && MMC_WRITE==TRUE)
// *******************************************************************************************************************************
// schreibt string auf karte, siehe ffwrite()
// ein string sind zeichen, '\0' bzw. 0x00 bzw dezimal 0 wird als string ende gewertet !!
// wenn sonderzeichen auf die karte sollen, lieber ffwrite benutzen!
// *******************************************************************************************************************************
void ffwrites( uint8_t *s ){
    while (*s){
    	ffwrite(*s++);
    }
    fat.bufferDirty = TRUE;
  }
#endif


#if (MMC_WRITEN==TRUE && MMC_WRITE==TRUE)
// *******************************************************************************************************************************
// schreibt n zeichen auf karte, siehe ffwrite()
// *******************************************************************************************************************************
void ffwriten( uint8_t *s, uint16_t n ){
	uint16_t i = 0;
    do{
    	ffwrite(*s++);
    }while( ++i < n );
    fat.bufferDirty = TRUE;
  }
#endif








