#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <string.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h> 
#include <sys/time.h>
#include <pthread.h>

#define PORT	 8080 
#define MAXNAME 128  //Massima grandezza nome del file   
#define FILE_NOT_FOUND "-2"
#define PATH_NAME "./files/"
#define MAXDATAPKT 512
#define ERRMSG "Il file non e' stato caricato correttamente sul server."
#define CNFMSG "Il file e' stato caricato correttamente."
#define MAXRTOERR "Il timer per l'invio del file e' scaduto." 
#define MAXSEQNUM 2000000000 //MAX_INT 2147483647
#define VALUE_RTO 500 //usec
#define PLOSTPKT 0.01
#define PLOSTACK 0.01

int sizewin;

void setWindowSize(int sizeWin){
	
	sizewin = sizeWin;
}

/*
Questa struttura è stata creata per simulare un segmento del livello di trasporto. Il contenuto dei file da inviare viene suddiviso in strutture di questo tipo. Questa struttura permette l'invio affidabile secondo il protocollo da noi progettato.
*/
struct packet{

	int sequenceNum;
	char data[MAXDATAPKT];
	uint16_t dataDim;
	bool lastPkt; 
	bool isAcked;
	bool isRetransmitted;
	int ackNum; 
	bool validAck;
	bool syn;
	bool fin;
};

/*
Questa struttura è stata creata per simulare un messagio di richiesta del client al server.
*/
struct pktRequest{
	int sequenceNum;
	char request[132]; //La dimensione e' data da MAXNAME(128) + 4 che è il numero di caratteri massimo che si può avere per un comando (incluso lo spazio)
	bool fin; //Richiesta di chiusura di connessione
};

/*
Questa struttura è stata creata per simulare un ack relativo a un segmento. Abbiamo preferito suddividere il segmento in questa maniera per migliorare l'efficienza nell'invio dei riscontri.
*/
struct pktAck{

	int ackNum;
};

struct packet pktRcv;

/*
Questa funzione permette la stampa rossa di un errore e l'uscita dall'applicazione.
*/
void error(char* msg){
	
	printf("\033[0;31m");
	printf("\nerrore %s\n", msg);
	perror("L' errore e' ");
	
	exit(EXIT_FAILURE);
}

/*
Questa funzione permette la stampa rossa di un errore ma non l'uscita dall'applicazione.
*/
void exception(char* msg , int sequenceNum){

	printf("\033[0;31m");
	if(sequenceNum > 0){
		printf("%s %d\n" , msg , sequenceNum);
	}else{
		printf("%s\n" , msg); 
	}
	printf("\033[0m");
}

/*
Questa funzione permette la stampa verde di un messaggio con esito positivo.
*/
void success(char* msg , int sequenceNum){

	printf("\033[0;32m");
	if(sequenceNum > 0){
		printf("%s %d\n" , msg , sequenceNum);
	}else{
		printf("%s\n" , msg); 
	}
	printf("\033[0m");
}

/*
Questa funzione restituisce il minimo fra due numeri. E' stata utilizzato perchè utile nella realizzazione del protocollo.
*/
int min(int num1, int num2){
	
	if(num1 < num2) return num1;
	
	return num2;
}

/*
Questa funzione restituisce il modulo di un numero con la base in input ed è stata realizzata perchè molto importante per poter realizzare lo scorrimento della finestra.
*/
int mod(int num, int base){

	if(num < 0) return base + num;	
	
	return num%base;
}

/*
Questa funzione stampa i numeri di sequenza all'interno della finestra a partire da sendBase o rcvBase. E' utile in fase di test per vedere il reale funzionamento del protocollo.
*/
void printWindow(struct packet** winSender, int base, bool sender){
	
	int j = base;
	
	//sender = 1, receiver = 0
	if(sender){

		printf("\nFINESTRA SENDER (sendBase=%d)\n|", base);
	}else{
		printf("\nFINESTRA RECEIVER (rcvBase=%d)\n|", base);
	}
		
	for(int i=0; i < sizewin; i++){
		printf(" %d |", (*winSender)[j].sequenceNum);
		j = (j + 1) % sizewin;	
	}
	printf("\n\n");
}

/*
Questa funzione alloca memoria pari a size byte e si occupa di controllarne la corretta esecuzione. Inoltre, dopo aver controllato che l'operazione sia avvenuta con successo viene pulita la porzione di memoria allocata in modo da renderla utilizzabile senza problemi.
*/
void* prepare_mem(size_t size, char* msg){
	
	void* mem_to_alloc = malloc(size);
	if(mem_to_alloc == NULL) error(msg);
	memset(mem_to_alloc , 0 , size);

	return mem_to_alloc;
}

/*
Questa funzione ha il compito, presi in input dei dati da inviare, di allocare pacchetti necessari per poter inviare ciò che è stato richiesto. I pacchetti vengono preparati secondo il protocollo.
Inoltre, la funzione ritorna il numero di pacchetti che sono stati creati.
*/
int prepare_packets(struct packet** pktToSend, char* dataToSend, int dimension, int* nextSeqNum){

	int numPkt = 0;
	int cnt = dimension;//La variabile cnt indica il numero di byte che mancano da inserire all'interno dei pacchetti
	
	//Verifica se il numero di sequenza non supera il valore massimo del numero di sequenza
	if(*nextSeqNum + dimension >= MAXSEQNUM){
		*nextSeqNum = 1;
	}

	//Calcolo del numero di pacchetti necessari
	if(dimension % MAXDATAPKT == 0){
		numPkt = (dimension / MAXDATAPKT);
	}else{
		numPkt = (dimension / MAXDATAPKT) + 1;
	}		

	*pktToSend = (struct packet*) prepare_mem(numPkt * sizeof(struct packet), "malloc pktToSend prepare_packets");

	//Algoritmo che permette la preparazione dei pacchetti
	for(int i = 0; i < numPkt; i++){
	
		(*pktToSend)[i].sequenceNum = *nextSeqNum;
		
		if(cnt>MAXDATAPKT){
		
			//Caso in cui i byte che mancano da inserire in un pacchetto sono più di MAXDATAPKT	
			memcpy((*pktToSend)[i].data, dataToSend+(i*MAXDATAPKT), MAXDATAPKT);
    			(*pktToSend)[i].lastPkt = 0;
			(*pktToSend)[i].dataDim = MAXDATAPKT;
			*nextSeqNum += MAXDATAPKT;
			cnt -= MAXDATAPKT;
		}else{
		
			//Caso in cui i byte che mancano da inserire in un pacchetto sono meno di MAXDATAPKT e quindi siamo giunti all'ultimo pacchetto
			memcpy((*pktToSend)[i].data, dataToSend+(i*MAXDATAPKT), cnt);
			((*pktToSend)[i].data)[cnt] = '\0';
    			(*pktToSend)[i].lastPkt = 1;
			(*pktToSend)[i].dataDim = cnt;
			*nextSeqNum += cnt;
		}
		
		(*pktToSend)[i].fin = 0;
	} 

	return numPkt;	
}

/*
Questa funzione permette l'invio di un pacchetto con perdita configurabile.
*/
void sendto_packet_with_lost(int socket, struct packet* packetToSend, struct sockaddr_in* addressDest){
	
	//int p = (rand() % 101);
	
	float p = ((float)rand())/RAND_MAX;
	
	if(p >= PLOSTPKT){
	
		#ifdef TEST
		success("PACCHETTO INVIATO CON SUCCESSO, NUMERO DI SEQUENZA =" , (*packetToSend).sequenceNum);
		#endif
		if(sendto(socket, packetToSend, sizeof(*packetToSend), 0, (const struct sockaddr *) addressDest, sizeof(*addressDest)) < 0) error("sendto");
	}else{
	
		#ifdef TEST
		exception("PACCHETTO INVIATO SENZA SUCCESSO, NUMERO DI SEQUENZA =" , (*packetToSend).sequenceNum);
		#endif		
	}
}

/*
Questa funzione permette l'invio di un ACK con perdita configurabile.
*/
void sendto_ack_with_lost(int socket, struct pktAck* ackToSend, struct sockaddr_in* addressDest, int prob){

	//int p = (rand() % 100);
	
	float p = ((float)rand())/RAND_MAX;
	
	if(p >= prob){
		#ifdef TEST
		success("ACK INVIATO CON SUCCESSO, NUMERO DI ACK =" , (*ackToSend).ackNum);
		#endif		

		if(sendto(socket, ackToSend, sizeof(*ackToSend), 0, (const struct sockaddr *) addressDest, sizeof(*addressDest)) < 0) error("sendto");
				
	}else{
		#ifdef TEST
		exception("ACK INVIATO SENZA SUCCESSO, NUMERO DI ACK =" , (*ackToSend).ackNum);
		#endif		
	}
}

/*
Questa funzione permette l'invio di pacchetti in maniera affidabile attraverso il protocollo implementato. Il sender, attraverso regole definite nel protocollo implementato, attende riscontri
da parte del receiver di ciò che lui ha inviato. Dopo aver ricevuto il riscontro per tutti i pacchetti da lui inviati, il sender può uscire dalla funzione.
*/
void sendto_reliable(int socket, struct sockaddr_in* addressDest,  struct packet* pktToSend, int numPkt, suseconds_t* rto){
	
	int messi_in_finestra = 0;
	int pacchetti_rimanenti = numPkt;	
	int pacchetti_riscontrati = 0;
	int sendBase = 0;
	int timerBase = sendBase;
	int len;
	int flyPkt = 0;
	int dupAckCount = 0;

	struct packet* winSender;
	struct sockaddr_in servaddr; 
	struct pktAck ack;
	struct timeval time_tx[sizewin];
	struct timeval timeout;
	struct timeval minTime;
		
	fd_set descriptor;
	
	memset(&ack, 0, sizeof(struct pktAck));
	
	//Alloco memoria per la finestra di spedizione
	winSender = (struct packet*) prepare_mem(sizewin * sizeof(struct packet), "malloc winSender");
	
	//Variabili utilizzare per il calcolo del timeout
	memset(time_tx, 0, sizewin * sizeof(struct timeval));
	
	memset(&timeout, 0, sizeof(struct timeval));
	
	//Setto le informazioni necessarie per l'RTO adattivo
	#ifdef ADAPTIVE_RTO
	struct timeval sampleRTT;
	
	memset(&sampleRTT, 0, sizeof(struct timeval));
	
	sampleRTT.tv_sec = 0;
	sampleRTT.tv_usec = 0;

	suseconds_t sampleRTT_usec = 0;
	suseconds_t estimatedRTT = 0;
	suseconds_t devRTT = 0;
	#endif
	
	srand(time(NULL));
	
	len = sizeof(servaddr);

	//Inizio della trasmissione affidabile
	while(pacchetti_rimanenti != 0){
	
		//Vengono spediti pacchetti finchè la dimensione della finestra lo permette
		for(int i=mod(sendBase - pacchetti_riscontrati,sizewin);flyPkt<sizewin && messi_in_finestra < numPkt;i++){
		
			//Viene inserito il pacchetto all'interno della finestra nella prima posizione disponibile
			winSender[i%sizewin] = pktToSend[messi_in_finestra];
			messi_in_finestra++;	
			
			//Viene inviato pacchetto
			sendto_packet_with_lost(socket, &(winSender[i%sizewin]), addressDest);

			//Viene preso il tempo di trasmissione			
			gettimeofday(&time_tx[i%sizewin], NULL);
			
			flyPkt++;
		}
		
		#ifdef TEST
		printWindow(&winSender, sendBase, 1);
		#endif

		timeout.tv_sec = 0;
		timeout.tv_usec = *rto;
		
		//Viene calcolato l'indice, a partire da sendBase, del pacchetto spedito maggior tempo fa in modo tale da azionare il timer su di esso
		int y = sendBase;
		
		minTime = time_tx[y];
		timerBase = y;
		
		for (int i=1; i<sizewin; i++) {
		
	    		if (timercmp(&minTime, &time_tx[y], >) != 0 && (time_tx[y].tv_sec != 0 && time_tx[y].tv_usec != 0)) {
	    
	        		minTime = time_tx[y];
	        		timerBase = y;
	   		}
	   		y = (y + 1)%sizewin;
	    	}
	    	
	    	//Vengono inizializzate le informazioni per il timeout
		FD_ZERO(&descriptor);
		FD_SET(socket, &descriptor);
		#ifdef TEST
		printf("\nrto = %ld\n" , *rto);
		#endif
		
		int fd_ready =  select(socket+1, &descriptor, NULL , NULL, &timeout);
		
		#ifdef ADAPTIVE_RTO
		gettimeofday(&sampleRTT, NULL);
		#endif		

		if(fd_ready == 0){
		
			//Caso in cui il timeout è scaduto
			#ifdef TEST
			printf("[TIMEOUT]");
			#endif

			//Invio il pacchetto di cui è scaduto il timer
			sendto_packet_with_lost(socket, &(winSender[timerBase]), addressDest);
			
			//Aggiorno il suo tempo di trasmissione
			gettimeofday(&time_tx[timerBase], NULL);
		
			//Segno il pacchetto come ritrasmesso. Ciò è utile nel calcolo del RTO adattivo
			winSender[timerBase].isRetransmitted = 1;
			
		}else if(fd_ready == 1){
		
			//Attendo un pacchetto di ACK
			len = sizeof(servaddr);
			if(recvfrom(socket, &ack, sizeof(ack), 0, (struct sockaddr *) &servaddr, &len) < 0) error("rcvfrom");
			
			
			if (ack.ackNum > winSender[sendBase].sequenceNum) {
			
				//Caso in cui l'ACK mi riscontra qualcosa di nuovo
				pacchetti_riscontrati = 0;
				
				//Algoritmo che mi permette di riscontrare pacchetti
				while(winSender[(sendBase + pacchetti_riscontrati) % sizewin].sequenceNum < ack.ackNum && pacchetti_riscontrati < min(sizewin, pacchetti_rimanenti)){
					
					//Controllo se il pacchetto non è stato già riscontrato
					if(winSender[(sendBase + pacchetti_riscontrati) % sizewin].sequenceNum != 0 && winSender[(sendBase + pacchetti_riscontrati) % sizewin].isAcked == 0){
						winSender[(sendBase + pacchetti_riscontrati) % sizewin].isAcked = 1;
						pacchetti_riscontrati++;
					}
				}
				
				//Calcolo il sampleRTT ignorando i segmenti ritrasmessi
				#ifdef ADAPTIVE_RTO
				if(pacchetti_riscontrati == 2 && winSender[mod(sendBase,sizewin)].isRetransmitted == 0 && winSender[mod(sendBase+1,sizewin)].isRetransmitted == 0){
					
					timersub(&sampleRTT, &time_tx[mod(sendBase+1,sizewin)], &sampleRTT);
					
					sampleRTT_usec = (suseconds_t)(sampleRTT.tv_sec * 1000000) + sampleRTT.tv_usec;
					
					estimatedRTT = (estimatedRTT-(estimatedRTT>>3)) + (sampleRTT_usec>>3);
					
					devRTT = (devRTT-(devRTT>>2)) + (abs(sampleRTT_usec - estimatedRTT)>>2);
			
					*rto = estimatedRTT + 4 * devRTT;
				}
				#endif
				
				//Calcolo il numero di pacchetti che rimangono da riscontrare
				pacchetti_rimanenti = pacchetti_rimanenti - pacchetti_riscontrati;
				
				//Traslo la finestra
				sendBase = (sendBase + pacchetti_riscontrati) % sizewin;
				
				//Tengo traccia dei pacchetti in volo
				flyPkt = flyPkt - pacchetti_riscontrati;
				
				//Azzero il contatore degli ack duplicati
				dupAckCount = 0;
				
			}else{
				
				//Ho ricevuto ack duplicato
				dupAckCount++;
							
				//Se ricevo 3 ack duplicati invio il pacchetto successivo
				if(dupAckCount == 3){
					#ifdef TEST
					printf("[FAST RETRANSMIT]");
					#endif
				
					//Invio il pacchetto attraverso il fast retransmit
					sendto_packet_with_lost(socket, &(winSender[timerBase]), addressDest);
					
					//Aggiorno il tempo di spedizione del pacchetto
					gettimeofday(&time_tx[timerBase], NULL);
					
					//Segno il pacchetto come ritrasmesso. Ciò è utile nel calcolo del RTO adattivo
					winSender[timerBase].isRetransmitted = 1;
					
					//Azzero il contatore degli ack duplicati
					dupAckCount = 0;
				}	
			}	
		}else{
			error("select");
		}
	}
	free(pktToSend);
}

/*
Questa funzione è utilizzata per far traslare la finestra lato receiver
*/
void slideWindow(int* rcvBase, struct packet** winReceiver, struct packet* pkt, int* lastSeqNumAcked, int* cnt){
	
	//Traslo la finestra
	for(int i = *rcvBase; (*winReceiver)[i%sizewin].sequenceNum != 0; i++){
		
		*lastSeqNumAcked += (*winReceiver)[i%sizewin].dataDim;
		pkt[*cnt] = (*winReceiver)[i%sizewin];
		*cnt = *cnt + 1;
		
		*rcvBase = (*rcvBase+1)%sizewin;
		
		memset(&((*winReceiver)[i%sizewin]), 0, sizeof(struct packet));
	}
}

/*
Questa funzione è utilizzata lato receiver nell'invio di risconti in maniera affidabile attraverso il protocollo implementato.Una volta ricevuti
tutti i pacchetti e una volta messi in ordine se l'ultimo pacchetto è stato bufferizzato, il receiver esce dalla funzione.
*/
void recvfrom_reliable(int socket, struct sockaddr_in* addressSrc , struct packet* pkt, int* lastSeqNumAcked){

	int cnt = 0;
	int posWin = 0;
	int rcvBase = 0;
	int fd_ready;

	struct packet pktRcv;
	struct packet* winReceiver;
	struct pktAck ack;
	struct timeval time_recv;

	socklen_t len; 
	
	fd_set descriptor;
	
	srand(time(NULL));

	//Allochiamo la finestra per la ricezione
	winReceiver = (struct packet*) prepare_mem(sizeof(struct packet) * sizewin, "malloc winReceiver recvfrom_reliable");
		
	memset(&ack, 0, sizeof(struct pktAck));
	memset(&time_recv, 0, sizeof(struct timeval));
	
	//Setto il numero di ack come ultimo riscontrato
	ack.ackNum = *lastSeqNumAcked;

	len = sizeof(*addressSrc);
	
	do{
		memset(&pktRcv, 0, sizeof(struct packet));

		//In attesa di pacchetti da ricevere
		if(recvfrom(socket, &pktRcv, sizeof(pktRcv), 0, (struct sockaddr *) addressSrc, &len) < 0) error("recvfrom");

		//Verifico se il numero di sequenza del pacchetto ricevuto è quello aspettato
		if(pktRcv.sequenceNum == *lastSeqNumAcked){

			//Inserisco il pacchetto nella prima posizione della finestra di ricezione
			winReceiver[rcvBase] = pktRcv;
			
			//Vedo se è possibile traslare la finestra e incremento il numero di sequenza che si sta riscontrando
			slideWindow(&rcvBase, &winReceiver, pkt, lastSeqNumAcked, &cnt);
			
			ack.ackNum = *lastSeqNumAcked;
			
			fd_ready = 0;
		
			//5000ms tempo scelto tra intervallo 0 - 1s in modo tale da non essere piu piccolo del timer lato sender. Serve per realizzare il meccanismo di delayed ack di TCP.
			time_recv.tv_sec = 0;
			time_recv.tv_usec = 5000;
			
			FD_ZERO(&descriptor);
			FD_SET(socket, &descriptor);
			
			//Verifico che il pacchetto non è l'ultimo pacchetto
			if(pktRcv.lastPkt != 1){
			
				//Attendo, come da protocollo, un periodo entro nel quale mi aspetto un nuovo pacchetto
				fd_ready = select(socket+1, &descriptor, NULL , NULL, &time_recv);

				if(fd_ready == -1) error("select recvfrom_reliable");
			
				if(fd_ready == 1){
				
					//Ricevo il pacchetto 
					if(recvfrom(socket, &pktRcv, sizeof(pktRcv), 0, (struct sockaddr *) addressSrc, &len) < 0) error("recvfrom");

					//Verifico se il pacchetto ricevuto è un pacchetto che mi aspettavo
					if(pktRcv.sequenceNum == *lastSeqNumAcked){
				
						//Inserisco il pacchetto nella prima posizione della finestra di ricezione
						winReceiver[rcvBase] = pktRcv;

						//Vedo se è possibile traslare la finestra e incremento il numero di sequenza che si sta riscontrando
						slideWindow(&rcvBase, &winReceiver, pkt, lastSeqNumAcked, &cnt);
						
						ack.ackNum = *lastSeqNumAcked;
					}else{
						//Calcolo la posizione del pacchetto in finestra visto che il pacchetto che ho ricevuto non è quello che mi aspettavo	
						if(pktRcv.sequenceNum > *lastSeqNumAcked){
							posWin = (pktRcv.sequenceNum - *lastSeqNumAcked) / MAXDATAPKT;						
							
							if(posWin < sizewin){
								posWin = (posWin+rcvBase)%sizewin;
								winReceiver[posWin] = pktRcv;
							}
						}
						#ifdef TEST
						printf("\n[DELAYED ACK]");
						#endif
					}					
				}
					
				//Invio di un ACK o DELAYED ACK	
				if(pkt[cnt-1].lastPkt == 1) {
					sendto_ack_with_lost(socket, &ack, addressSrc, 0);
				}else{
					sendto_ack_with_lost(socket, &ack, addressSrc, PLOSTACK);
				}

			}else{
				//Invio ultimo ack senza perdita
				sendto_ack_with_lost(socket, &ack, addressSrc, 0);
			}
			
		}else{
			//Calcolo la posizione del pacchetto in finestra visto che il pacchetto che ho ricevuto non è quello che mi aspettavo		
			if(pktRcv.sequenceNum > *lastSeqNumAcked){
				posWin = (pktRcv.sequenceNum - *lastSeqNumAcked) / MAXDATAPKT;
			
				if(posWin < sizewin){
					
					posWin = (posWin+rcvBase)%sizewin;
					winReceiver[posWin] = pktRcv;
				}
			}
			
			ack.ackNum = *lastSeqNumAcked;
			
			//Invio di un ACK DUPLICATO
			if(pkt[cnt-1].lastPkt == 1) {
				sendto_ack_with_lost(socket, &ack, addressSrc, 0);
			}else{
				sendto_ack_with_lost(socket, &ack, addressSrc, PLOSTACK);
			}
		}
		#ifdef TEST
		printWindow(&winReceiver, rcvBase, 0);
		#endif

	}while(pkt[cnt-1].lastPkt != 1);

	free(winReceiver);
}	


