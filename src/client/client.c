#include <signal.h>
#include <malloc.h>
#include "../utils.c"

#define MAXCOMMAND 512
#define MAXLIST 1024
#define SERVERADDRESS "127.0.0.1"

int sockfd;
int len;
int nextSeqNum = 1;
int lastSeqNumAckedRcv = 1;
int numPkt;

char portaDest[5];

bool connesso = 0;

//Viene inizializzato l'rto iniziale con il quale il client inizia a trasmettere con il server
suseconds_t rto = VALUE_RTO;

struct packet* pktRcvData;
struct packet* pktSender;
struct sockaddr_in addressThreadDest;

/*
Questa funzione viene eseguita in due casi:

-Il server è giù e il client tenta di instaurare una connessione, dopo un tempo di attesa senza risposta viene comunicato il messaggio al client ed esce dall'applicazioe.

-Il client ha richiesto la chiusura della connessione e scade il timer di attesa di 4 minuti.
*/
void timeout_server(){
	
	//Verifichiamo lo stato del client per decide quale messaggio comunicare
	if(!connesso) {
		exception("\nTempo scaduto per la connessione al server.\n" , -1);
	}else{
		success("\nTimer 4 min scaduto, chiudo connessione\n" , -1);
	}
	exit(EXIT_FAILURE);
}

/*
Questa funzione gestite la chiusura della connessione seguendo l'implementazione utilizzate nel protocollo TCP.
*/
void termina_connessione(){

	struct pktRequest pkt;
	struct packet ack;
	struct timeval time_close;

	fd_set descriptor;	

	len = sizeof(addressThreadDest);
	memset(&pkt , 0 , sizeof(struct pktRequest));
	memset(&ack , 0 , sizeof(struct packet));
	
	pkt.fin = 1;
	pkt.sequenceNum = nextSeqNum;

	//Invio il pacchetto FIN al server
	if(sendto(sockfd, &pkt, sizeof(pkt), 0, (const struct sockaddr *) &addressThreadDest, sizeof(addressThreadDest)) < 0) 
				error("sendto");

	//Attendo il pacchetto di ACK del FIN che ho inviato
	if(recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *) &addressThreadDest, &len) < 0) error("recvfrom connect");

	while(1){
		
		//Attendo il pacchetto di FIN dal server
		memset(&pkt , 0 , sizeof(struct packet));
		if(recvfrom(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *) &addressThreadDest, &len) < 0) error("recvfrom connect");
		
		//Preparo il pacchetto di ACK da inviare al server
		memset(&ack , 0 , sizeof(struct packet));
		ack.ackNum = pkt.sequenceNum + 1;	
		ack.validAck = 1; 

		//Invio il pacchetto di ACK del FIN al server e attendo un tempo di 4min per riuscire a gestire evenutale ritrasmissione FIN del server
		if(sendto(sockfd, &ack, sizeof(ack), 0, (const struct sockaddr *) &addressThreadDest, sizeof(addressThreadDest)) < 0) error("sendto");

		memset(&pkt , 0 , sizeof(struct packet));
		
		//Inizializzo i dati per effettuare la select che attende un'eventuale ritrasmissione
		FD_ZERO(&descriptor);
		FD_SET(sockfd, &descriptor);

		memset(&time_close , 0 , sizeof(struct timeval));
		time_close.tv_sec = 240;
		time_close.tv_usec = 0;
		
		#ifdef TEST
		printf("\nHo attivato timer\n");
		#endif
		int fd_ready = select(sockfd+1, &descriptor, NULL , NULL, &time_close);
		if(fd_ready == 0){
		
			//Caso in cui è scaduto il timer di 4 minuti e quindi è andato tutto bene
			#ifdef TEST
			success("\n-- Timer scaduto, la disconnessione e' avvenuta con successo --\n" , -1);
			#endif
			
			exit(1);
		}else if(fd_ready == 1){
			
			//Caso in cui il client è pronto in lettura, ossia il server ha ritrasmesso il pacchetto di FIN al client
			#ifdef TEST
			printf("\nNon e' scaduto il timer ricevo un nuovo fin\n");
			#endif
		}else{
			error("select client timer");		
		}
	}
}

/*
Questa funzione gestite la instaurazione di una connessione seguendo l'implementazione utilizzata nel protocollo TCP, handshaking a  3 vie.
*/
bool connessione_server(struct sockaddr_in servaddr){
	
	pktSender = (struct packet*)prepare_mem(2*sizeof(struct packet), "malloc pktSender connessione_server");

	//Invio pacchetto di SYN al server
	pktSender[0].syn = 1;
	if(sendto(sockfd, &pktSender[0], sizeof(pktSender[0]), 0, (const struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
				error("sendto");
	
	len = sizeof(servaddr);
	alarm(5);
	
	//Attendo il pacchetto di SYN-ACK dal server
	if(recvfrom(sockfd, &pktRcv, sizeof(pktRcv), 0, (struct sockaddr *) &servaddr, &len) < 0) error("recvfrom connect");

	//Vengono salvate le informazioni relative a stabilire una connessione con il thread del server dedicato
	strncpy(portaDest , pktRcv.data , strlen(pktRcv.data));
	
	//Setto la dimensione della finestra
	setWindowSize(pktRcv.ackNum);
	
	//Verifico se il pacchetto è il SYN-ACK aspettato
	if(pktRcv.syn == 1 && pktRcv.validAck == 1 && pktRcv.ackNum >= pktSender[0].sequenceNum + 1){
	
		//Preparo pacchetto di ACK riscontrando il numero di sequenza del pacchetto di SYN-ACK che è stato settato a 1
		pktSender[1].validAck = 1;
		pktSender[1].ackNum = pktRcv.sequenceNum + 1;
		pktSender[1].sequenceNum = pktRcv.ackNum;
		
		if(sendto(sockfd, &pktSender[1], sizeof(pktSender[1]), 0, (const struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
			error("sendto");
		
		//Setto la socket destinazione del thread dedicato del server
		memset(&addressThreadDest, 0, sizeof(addressThreadDest)); 
		addressThreadDest.sin_family = AF_INET; 
		addressThreadDest.sin_port = atoi(portaDest);
		if (inet_pton(AF_INET, SERVERADDRESS, &addressThreadDest.sin_addr) <= 0) {
	   		error("pton");
	  	}
	  	
	  	connesso = 1;
		
		success("Connessione stabilita con il server." , -1);
		
	}else if(pktRcv.syn == 0){
	
		//Caso in cui il server non ha porte disponibili per stabilire una connessione con il client
		fprintf(stderr, "\033[1;31m");
		fprintf(stderr, "\nIl server non e' in grado di accettare la connessione.\n");
		exit(EXIT_FAILURE);
	}
	
	free(pktSender);
	return connesso;
}

/*
Questa funzione stampa uno script che permette al client, dopo aver effettuato una richiesta, di capire che il server sta elaborando la sua richiesta.
*/
void* loading_script(){

	while(1){
		for(int each = 0; each < 4; ++each) {
			printf ("\rloading%.*s   \b\b\b", each, "...");
			fflush (stdout);//force printing as no newline in output
			usleep (375000);
		}
	}
}

int main(int argc, char* argv[]) { 

	pthread_t thread_script;

	char scelta[MAXCOMMAND];
	char fileName[MAXNAME];
	char path[136] = PATH_NAME; 

	struct sigaction act;
	struct packet pktClose;
	struct pktRequest pktReq;
	struct timeval request_time_start;
	struct timeval request_time_fin;
	struct sockaddr_in servaddr;

	FILE* file;
	sigset_t set;
	fd_set descriptor; 
	
	memset(&request_time_start, 0, sizeof(struct timeval));
	request_time_start.tv_sec = 0;
	request_time_start.tv_usec = 0;

	memset(&request_time_fin, 0, sizeof(struct timeval));
	request_time_fin.tv_sec = 0;
	request_time_fin.tv_usec = 0;
	
	//Viene verificato il corretto lancio dell'applicazione
	if (argc != 1) {
		error("utilizzo: ./client");
	}

	//Vengono settate le informazioni per comunicare con il main-thread del server
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		error("creazione socket fallita"); 
		exit(EXIT_FAILURE); 
	} 

	memset(&servaddr, 0, sizeof(servaddr)); 
	
	servaddr.sin_family = AF_INET; 
	servaddr.sin_port = htons(PORT); 
	if (inet_pton(AF_INET, SERVERADDRESS, &servaddr.sin_addr) <= 0) {
   		error("pton");
  	}

	//Vengono settate le informazioni per gestire i segnali di chiusura applicazione
	act.sa_sigaction = timeout_server;	
	act.sa_mask = set;
	act.sa_flags = 0;
	
	if(sigaction(SIGALRM, &act, NULL) == -1) error("sigaction SIGALRM");
	
	if(connessione_server(servaddr)) alarm(0);

	sigfillset(&set);
	act.sa_sigaction = termina_connessione;	
	act.sa_mask = set;
	act.sa_flags = 0;
	
	if(sigaction(SIGINT, &act, NULL) == -1) error("sigaction SIGINT");
	if(sigaction(SIGQUIT, &act, NULL) == -1) error("sigaction SIGQUIT");
	if(sigaction(SIGTSTP, &act, NULL) == -1) error("sigaction SIGTSTP");	
	
	while(1){

		printf("\nEsegui uno dei seguenti comandi: \n\n");
		printf("1. [list] per visualizzare la lista dei files presenti sul server\n");
		printf("2. [get nomefile] per effettuare il download del file 'nomefile' dal server\n");
		printf("3. [put nomefile] per effettuare l'upload del file 'nomefile' sul server\n");
		printf("4. [exit] per chiudere la connessione con il server\n\n");

		//In attesa della richiesta del client
		if(fgets(scelta, MAXCOMMAND, stdin) == NULL){
			if(errno != EINTR)
				error("fgets\n");
		}
	
		//Vengono settate le informazioni per far si che il client, dopo un periodo di tempo che non effettua richieste, riceva un pacchetto di FIN dal thread dedicato		
		FD_ZERO(&descriptor);
		FD_SET(sockfd, &descriptor);

		struct timeval close;
		close.tv_sec = 0;
		close.tv_usec = 0;

		int fd_ready = select(sockfd+1, &descriptor, NULL , NULL, &close);
		if(fd_ready == 1){
		
			//Caso in cui è stato ricevuto il pacchetto di FIN dal thread dedicato
			memset(&pktClose , 0 , sizeof(struct packet));

			len = sizeof(servaddr); 
			if(recvfrom(sockfd, &pktClose , sizeof(pktClose), 0, (struct sockaddr *) &servaddr, &len) < 0) error("recv");
			
			if(pktClose.fin == 1){

				exception("Timeout richieste al server" , -1);
				exit(-1);
			}

		}else if(fd_ready == -1){
			error("select main");
		}
		
		//Sovrascrizione del carattere '\n' catturato dalla fgets al click del tasto ENTER
		scelta[strlen(scelta)-1]='\0';		
	
		memset(&pktReq, 0, sizeof(struct pktRequest));
		strcpy(pktReq.request, scelta);
		pktReq.fin = 0;
		
		//Scelta list
		if(strcmp(scelta, "list") == 0){
				
			//Viene creato il thread che effettua lo script di loading
			if(pthread_create(&thread_script, NULL, loading_script, NULL) != 0) error("pthread_create");
			
			//Invio il pacchetto di richiesta list
			if(sendto(sockfd, &pktReq, sizeof(pktReq), 0, (const struct sockaddr *) &addressThreadDest, sizeof(addressThreadDest)) < 0)
			error("sendto");

			//Ricevo il numero di pacchetti che riceverò
			len = sizeof(servaddr); 
			if(recvfrom(sockfd, &numPkt , sizeof(numPkt), 0, (struct sockaddr *) &servaddr, &len) < 0) error("recv");
			
			//Alloco memoria per il numero di pacchetti che riceverò
			pktRcvData = (struct packet*) prepare_mem(numPkt * sizeof(struct packet), "malloc pktRcvData main list");
		
			//Prendo il tempo di inizo di ricevimento pacchetti
			gettimeofday(&request_time_start, NULL);
			
			//Ricevo i pacchetti secondo il protocollo
			recvfrom_reliable(sockfd , &servaddr, pktRcvData, &lastSeqNumAckedRcv);
			
			//Prendo il tempo di fine di ricevimento pacchetti
			gettimeofday(&request_time_fin, NULL);
			
			if(pthread_cancel(thread_script) != 0) error("pthread_cancel");
			
			printf("\033[2J\033[H");

			//Stampo il contenuto dei pacchetti ricevuti
			printf("Lista file disponibili Server:\n\n");
			for(int i = 0; i < numPkt; i++){
				printf("%s", pktRcvData[i].data);
			}
			printf("\n");

			free(pktRcvData);

		//Scelta get			
		}else if(strncmp(scelta, "get ", 4) == 0){

			printf("\033[2J\033[H");

			//Viene creato il thread che effettua lo script di loading
			if(pthread_create(&thread_script, NULL, loading_script, NULL) != 0) error("pthread_create");	
		
			//Invio il pacchetto di richiesta get
			if(sendto(sockfd, &pktReq, sizeof(pktReq), 0, (const struct sockaddr *) &addressThreadDest, sizeof(addressThreadDest)) < 0)
			error("sendto");

			//Ricevo il numero di pacchetti che riceverò
			len = sizeof(servaddr); 
			if(recvfrom(sockfd, &numPkt , sizeof(numPkt), 0, (struct sockaddr *) &servaddr, &len) < 0) error("recv");
			
			//Alloco memoria per il numero di pacchetti che riceverò
			pktRcvData = (struct packet*) prepare_mem(numPkt * sizeof(struct packet), "malloc pktRcvData main get");
			
			//Prendo il tempo di inizo di ricevimento pacchetti
			gettimeofday(&request_time_start, NULL);
			
			//Ricevo i pacchetti secondo il protocollo
			recvfrom_reliable(sockfd , &servaddr, pktRcvData, &lastSeqNumAckedRcv);
			
			//Prendo il tempo di fine di ricevimento pacchetti
			gettimeofday(&request_time_fin, NULL);			

			//Copia su fileName l'effettivo nome del file
			strcpy(fileName , scelta + 4);
			strcat(path , fileName);
			
			if(pthread_cancel(thread_script) != 0) error("pthread_cancel");
			
			printf("\033[2J\033[H");

			if(strcmp(pktRcvData[0].data, FILE_NOT_FOUND) == 0 ){
			
				//Caso in cui il client ha richiesto un file non presente nel server
				char* msg;
				msg = (char*)prepare_mem(sizeof(char) + 48 + strlen(fileName), "malloc msg get");

				strcat(msg , "Il file '");
				strcat(msg , fileName);
				strcat(msg, "' richiesto non e' presente nel server.");

				exception(msg , -1);
				free(msg);
			}else{
				
				//Caso in cui il client ha richiesto un file presente nel server
				file = fopen(path, "w+");
				if(file == NULL) error("fopen get client");
				
				//Scrittura del contenuto dei pacchetti ricevuti all'intero del file
				for(int i=0; i < numPkt ; i++){		
					if(fwrite(pktRcvData[i].data, 1, pktRcvData[i].dataDim, file) <= 0) error("fwrite get client");
				}
				
				if(fclose(file) == EOF) error("fclose client");

				char* msg;
				msg = (char*)prepare_mem(sizeof(char) + 44 + strlen(fileName), "malloc msg get");
				
				strcat(msg , "Il file '");
				strcat(msg , fileName);
				strcat(msg, "' e' stato scaricato correttamente.");
				
				success(msg , -1);
				free(msg);
			}
		
			free(pktRcvData);
	
			//Viene pulito il path eliminando il nome del file caricato precedentemente
			memset(path+8, 0, strlen(fileName));
			
		//Scelta put
		}else if(strncmp(scelta, "put ", 4) == 0){

			printf("\033[2J\033[H");

			FILE* file;
			char path[136] = PATH_NAME; //La dimensione rispetta la lunghezza di MAXNAME piu la dimensione dei caratteri per spostarsi nella cartella "files"
			int size;

			strcpy(fileName , scelta + 4);
			strcat(path , fileName);

			bool isEmpty = 1;

			file = fopen(path, "r");
			
			//Viene verificato se il nome del file da caricare inserito dal client è la stringa vuota
			if(strcmp(fileName, "") == 0) isEmpty = 0;
			
			if(file == NULL || isEmpty == 0){
				if(errno == ENOENT || isEmpty == 0){
	
					//Viene pulito il tempo di elaborazione della richiesta
					memset(&request_time_start, 0, sizeof(struct timeval));	
					memset(&request_time_fin, 0, sizeof(struct timeval));
					
					//Stampa del messaggio che informa al client che il file che ha richiesto non esiste
					char* msg;
					msg = (char*)prepare_mem(sizeof(char) + 22 + strlen(fileName), "malloc msg put");

					strcat(msg , "Il file '");
					strcat(msg , fileName);
					strcat(msg, "' non esiste.");
					exception(msg , -1);	
					free(msg);
					
				}else{
					error("fopen");
				}
			}else{
				
				//Viene creato il thread che effettua lo script di loading
				if(pthread_create(&thread_script, NULL, loading_script, NULL) != 0) error("pthread_create");

				//Ci posizioniamo alla fine del file al fine di calcolarne le dimensioni
				fseek(file, 0, SEEK_END);
				size = ftell(file);
				if(size == -1) error("ftell");
				fseek(file, 0, SEEK_SET);
				
				//Invio il pacchetto di richiesta di put
				if(sendto(sockfd, &pktReq, sizeof(pktReq), 0, (const struct sockaddr *) &addressThreadDest, sizeof(addressThreadDest)) < 0)
					error("sendto");

				//Leggo il contenuto del file da inviare
				char* buffer= (char*)prepare_mem(sizeof(char)*size + 1, "malloc buffer put");
				
				if(fread(buffer, 1, size, file) <=  0) error("fread"); 
			
				//Preparo i pacchetti e invio il file al server
				numPkt = prepare_packets(&pktSender, buffer, size, &nextSeqNum);
			
				free(buffer);
				
				//Invio al server il numero di pacchetti che riceverà
				if(sendto(sockfd, &numPkt , sizeof(numPkt), 0, (const struct sockaddr *) &addressThreadDest, sizeof(addressThreadDest)) < 0) error("sendto");

				//Prendo il tempo di inizo di ricevimento pacchetti
				gettimeofday(&request_time_start, NULL);
				
				//Invio il file secondo il protocollo
				sendto_reliable(sockfd, &addressThreadDest, pktSender, numPkt, &rto);
				
				//Prendo il tempo di fine di ricevimento pacchetti
				gettimeofday(&request_time_fin, NULL);
				
				//Alloco memoria per la risposta
				pktRcvData = (struct packet*) prepare_mem(sizeof(struct packet), "malloc pktRcvData put");

				//Ricevo il pacchetto di risposta secondo il protocollo
				recvfrom_reliable(sockfd , &servaddr, pktRcvData, &lastSeqNumAckedRcv);
				//if(recvfrom(sockfd, &(pktRcvData[0]) , sizeof(pktRcvData[0]), 0, (struct sockaddr *) &servaddr, &len) < 0) error("recv");
				
				if(pthread_cancel(thread_script) != 0) error("pthread_cancel");
				
				printf("\033[2J\033[H");
				
				//Stampa del messaggio di risposta dal server
				if(strcmp(pktRcvData[0].data , CNFMSG) == 0){

					//Caso in cui l'esito inviato dal server è positivo
					char* msg;
					msg = (char*)prepare_mem(4 + strlen(CNFMSG) + strlen(fileName), "malloc msg put CNF");
					strncat(msg, CNFMSG , 8);
					strcat(msg , "'");
					strcat(msg , fileName);
					strcat(msg , "' ");
					strcat(msg , CNFMSG + 8);

					success(msg , -1);
					free(msg);
				}else{
				
					//Caso in cui l'esito inviato dal server è negativo
					char* msg;
					msg = (char*)prepare_mem(4 + strlen(ERRMSG) + strlen(fileName), "malloc msg put ERR");

					strncat(msg, ERRMSG , 8);
					strcat(msg , "'");
					strcat(msg , fileName);
					strcat(msg , "' ");
					strcat(msg , ERRMSG + 8);
			
					exception(msg , -1);
					free(msg);
				}
				
				free(pktRcvData);
				
				if(fclose(file) == EOF) error("fclose");
			}

		//Scelta exit
		}else if(strcmp(scelta, "exit") == 0){
			termina_connessione();
		//Input inserito dal client non corretto
		}else{
			printf("\033[2J\033[H");
			
			//Viene pulito il tempo di elaborazione della richiesta
			memset(&request_time_start, 0, sizeof(struct timeval));	
			memset(&request_time_fin, 0, sizeof(struct timeval));
			
			if(errno != EINTR)
				exception("L'input inserito non e' corretto.\n" , -1);
		}
	
		//Calcolo il tempo di trasferimento dati
		timersub(&request_time_fin, &request_time_start , &request_time_fin);
		
		#ifdef TEST
		printf("\nTEMPO DI TRASFERIMENTO FILE: %ld sec %ld usec\n", request_time_fin.tv_sec, request_time_fin.tv_usec);
		#endif
		memset(fileName , 0 , MAXNAME);
		memset(scelta, 0, MAXCOMMAND);
	} 

	return 0; 
} 

