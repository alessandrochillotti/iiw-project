#include <dirent.h>
#include "../utils.c"

#define MAXPORT 2023
#define AVAILABLEPORT 100
#define FIRSTPORT 1024 // Le prime 1023 porte sono dedicate per processi noti e processi di sistema
#define MAXLIST 4096

/*
Questa struttura consente di memorizzare quale risorsa viene utilizzata per rendere l'accesso atomico in scrittura per il file di nome 'fileName'
*/
struct file_lock{
	
	char fileName[MAXNAME];
	pthread_rwlock_t lock_rw/* = PTHREAD_RWLOCK_INITIALIZER*/;
	struct file_lock* next;
};
typedef struct file_lock FileLock;
typedef FileLock* ListFileLock;

ListFileLock listlockFileGets;

int port = FIRSTPORT;

struct packet pkt;

//Mutex utilizzato per l'accesso in scrittura nella lista collegata di file_lock
pthread_mutex_t mutexList;

//Array dei threads che gestiscono le richieste. Il valore nella posizione i è il tid che gestisce il client connesso alla porta numero i. Se valore = 0, connessione non attiva.
pthread_t threads[AVAILABLEPORT];

/*
Questa funzione permette l'inserimento del primo elemento della lista collegata
*/
void insertHead(ListFileLock *listFileLock, char* fileName){

	ListFileLock new;   

	new = malloc(sizeof(FileLock));
	if (new == NULL) error("malloc new");

	strcpy(new->fileName , fileName);
	if(pthread_rwlock_init(&(new->lock_rw), NULL) != 0) error("error pthread_rwlock_init");
	new->next = *listFileLock;
	*listFileLock = new;
}

/*
Questa funzione permette l'inserimento di un elemento alla fine della lista collegata
*/
void insertFileLock(ListFileLock *listFileLock, char* fileName){

	ListFileLock new;
	ListFileLock previous;
	ListFileLock current;
	
	new = malloc(sizeof(FileLock));
   	if (new == NULL) error("malloc new");
   	
	strcpy(new->fileName , fileName);
	if(pthread_rwlock_init(&(new->lock_rw), NULL) != 0) error("error pthread_rwlock_init");
	new->next = NULL;

	previous = NULL;
	current = *listFileLock;

	//Scorro fino all'ultimo
	while (current != NULL) {
 		previous = current;
 		current = current->next;
	}

 	new->next = *listFileLock;
 	*listFileLock = new;
}

/*
Questa funzione restituisce l'indirizzo della risorsa che permette di accedere in maniera atomica (quando si tratta di una scrittura) su un determinato file con nome fileName. Questa funzione viene
eseguita in caso di get, poichè il thread chiede il token in lettura alla risorsa solamente quando il file esiste realmente all'interno del server.
*/
pthread_rwlock_t* lockFileGet(ListFileLock *fileLockToAdd, char* fileName){

	bool found = 0;
			
	ListFileLock previous;
	ListFileLock current;
	
	previous = NULL;
	current = *fileLockToAdd;

	//Questo ciclo permette di fermarsi appena si trova il fileName desiderato oppure ci si ferma solamente al raggiungimeneto dell'ultimo elemento
	while (found == 0 && current != NULL) {

		if(strcmp(current->fileName, fileName) == 0){
			found = 1;
		}else{
 			previous = current;
 			current = current->next;
 		}
	}
	
	return &(current->lock_rw);
}

/*
Questa funzione restituisce l'indirizzo della risorsa che permette di accedere in maniera atomica (quando si tratta di una scrittura) su un determinato file con nome fileName. Se l'elemento non è
già presente all'interno del server, verrà creata un nuovo elemento della lista collegate e verrà assegnato ad esso la risorsa che permette l'accesso atomico ad esso in caso di scrittura. Ovviamente,
in caso di sole letture la risorsa permette l'accesso al file simultaneo. E' stato implemenato un accesso esclusivo in scrittura alla lista collegata.
*/
pthread_rwlock_t* lockFilePut(ListFileLock *fileLockToAdd, char* fileName){

	bool found = 0;
			
	ListFileLock previous;
	ListFileLock current;
	
	pthread_rwlock_t* res;
	
	previous = NULL;
	current = *fileLockToAdd;
	
	//Presa del token per accedere in scrittura alla lista collegata
	pthread_mutex_lock(&mutexList);
	
	//Questo ciclo permette di fermarsi appena si trova il fileName desiderato oppure ci si ferma solamente al raggiungimeneto dell'ultimo elemento
	while (found == 0 && current != NULL) {

		if(strcmp(current->fileName, fileName) == 0){
			found = 1;
		}else{
 			previous = current;
 			current = current->next;
 		}
	}
	
	//Se il file è stato trovato si ritorna l'indirizzo alla risorsa esistente, altrimenti si crea l'elemento della lista e si ritorna l'indirizzo alla risorsa creato
	if(found){
		
		res = &(current->lock_rw);
	}else{
	
		ListFileLock new;
		new = malloc(sizeof(FileLock));
		if (new == NULL) error("malloc new");
		
		strcpy(new->fileName , fileName);
		if(pthread_rwlock_init(&(new->lock_rw), NULL) != 0) error("error pthread_rwlock_init");
		new->next = NULL;
		
		previous->next = new;
		
		res = &((previous->next)->lock_rw);
	}
	
	//Rilascio del token per accedere in scrittura alla lista collegata
	pthread_mutex_unlock(&mutexList);
	
	return res;
}

/*
Questa funzione elabora la richiesta list del client inviando, tramite il protocollo implementato, i pacchetti contententi i nomi dei files presenti nel server.
*/
void list(struct sockaddr_in addressClient, int sockThread, struct packet* pktToSend, int* nextSeqNum, suseconds_t* rto){

	int numPkt = 0;
	int size = 0;
	
	char* lista;	

	struct dirent *dir; 
	
	DIR *d;

	#ifdef TEST
	printf("Il thread %ld ha fatto list \n" , pthread_self());
	#endif

	//Algoritmo che permette di estrapolare i nomi dei files all'interno della cartella files
	if((d = opendir("./files")) == NULL){
		error("opendir");
	}else{
		//Calcolo della lunghezza della lista per allocare un array di dimensioni corrette.
		while((dir = readdir(d)) != NULL){
			if(dir->d_type == DT_REG){
				size += strlen(dir->d_name) + 4;
			}
		}

		//Alloco memoria sufficiente per contenere la lista
		lista = (char*) prepare_mem(sizeof(char) * size + 1, "malloc lista");

		//Inserisco i nomi dei files all'interno della lista
		rewinddir(d);
		while((dir = readdir(d)) != NULL){
			if(dir->d_type == DT_REG){
				strcat(lista , "•");
				strcat(lista, dir->d_name);
				strcat(lista, "\n");
			}
		}
		if(closedir(d) != 0) error("close dir");	
	}	

	lista[size] = '\0';

	//Preparo i pacchetti per inviare la lista
	numPkt = prepare_packets(&pktToSend, lista, size, nextSeqNum);

	//Invio il numero di pacchetti che invierò al client
	if(sendto(sockThread , &numPkt , sizeof(numPkt) , 0 , (const struct sockaddr *) &addressClient,sizeof(addressClient)) < 0)
				error("sendto");

	//Invio i pacchetti secondo il protocollo implementato
	sendto_reliable(sockThread, &addressClient, pktToSend, numPkt, rto);
	
	free(lista);
}

/*
Questa funzione elabora la richiesta get del client inviando, tramite il protocollo implementato, i pacchetti contententi i dati del file richiesto dal client.
*/
void get(char* fileName, struct sockaddr_in addressClient,  int sockThread, struct packet* pktToSend, int* nextSeqNum, suseconds_t* rto){

	int size;
	int numPkt;

	char path[136] = PATH_NAME; //La dimensione rispetta la lunghezza di MAXNAME piu la dimensione dei caratteri per spostarsi nella cartella "files"
	bool isEmpty = 1;

	FILE* file = NULL;
	
	pthread_rwlock_t* lock_res;

	#ifdef TEST
	printf("Il thread %ld ha fatto get \n" , pthread_self());
	#endif

	strcat(path , fileName);

	//Viene verificato se il nome del file da caricare inserito dal client è la stringa vuota
	if(strcmp(fileName , "") == 0) isEmpty = 0;
	
	file = fopen(path, "r");
	
	if(file == NULL || isEmpty == 0){
		if(errno == ENOENT || isEmpty == 0){
		
			//Preparo il pacchetto da inviare
			numPkt = prepare_packets(&pktToSend, FILE_NOT_FOUND, strlen(FILE_NOT_FOUND), nextSeqNum);
	
			//Invio il numero di pacchetti che invierò al client
			if(sendto(sockThread , &numPkt , sizeof(numPkt) , 0 , (const struct sockaddr *) &addressClient,sizeof(addressClient)) < 0)
				error("sendto");
			
			//Invio il pacchetto secondo il protocollo
			sendto_reliable(sockThread, &addressClient, pktToSend, 1, rto);

			return;
		}else{
			error("fopen");
		}
	}

	//Mi posiziono alla fine del file per calcolarne la dimensione
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	if(size == -1) error("ftell");
	
	//Mi riposiziono all'inizio del file
	fseek(file, 0, SEEK_SET);

	//Alloco memoria per bufferizzare il contenuto del file
	char* buffer= (char*)prepare_mem(sizeof(char)*size + 1, "malloc buffer");
	
	lock_res = lockFileGet(&listlockFileGets, path+strlen(PATH_NAME));
	
	if (pthread_rwlock_rdlock(lock_res) != 0) {
       	error("reader_thread: pthread_rwlock_rdlock error");
      		exit(__LINE__);
    	}
	
	//Leggo il contenuto del file da inviare
	if(fread(buffer, 1, size, file) < 0) error("fread"); 
	
	if (pthread_rwlock_unlock(lock_res) != 0) {
       	error("reader_thread: pthread_rwlock_rdlock error");
      		exit(__LINE__);
    	}

	//Preparo i pacchetti e invio il file al server
	numPkt = prepare_packets(&pktToSend, buffer, size, nextSeqNum);
	
	//Invio il numero di pacchetti che invierò al client
	if(sendto(sockThread , &numPkt , sizeof(numPkt) , 0 , (const struct sockaddr *) &addressClient,sizeof(addressClient)) < 0)
				error("sendto");
	
	if(fclose(file) == EOF) error("fclose server");
	free(buffer);
	
	//Invio i pacchetti tramite il protocollo 
	sendto_reliable(sockThread, &addressClient, pktToSend, numPkt, rto);
}

/*
Questa funzione elabora la richiesta put del client ricevendo, tramite il protocollo implementato, i pacchetti contententi i dati del file caricato dal client.
*/
void put(char* path, struct sockaddr_in addressClient,  int sockThread, struct packet* pktToSend,int* nextSeqNum, int* lastSeqNumAckedRcv, suseconds_t* rto){

	int numPkt = 0;

	bool success = 1;

	struct sockaddr_in servaddr;
	struct packet* pktRcvData;

	pthread_rwlock_t* lock_res;

	socklen_t len; 	
	
	FILE* file;

	#ifdef TEST
	printf("Il thread %ld ha fatto put \n" , pthread_self());	
	#endif

	//Ricevo il numero di pacchetti che riceverò
	len = sizeof(servaddr); 
	if(recvfrom(sockThread, &numPkt , sizeof(numPkt), 0, (struct sockaddr *) &servaddr, &len) < 0) error("recv");
	
	//Alloco memoria per il numero di pacchetti che riceverò
	pktRcvData = (struct packet*) prepare_mem(numPkt * sizeof(struct packet), "malloc pktRcvData");
	
	//Ricevo i pacchetti secondo il protocollo
	recvfrom_reliable(sockThread , &servaddr, pktRcvData, lastSeqNumAckedRcv);

	//Apro il file in scrittura
	file = fopen(path, "w+");
	if(file == NULL) error("fopen"); 
	
	lock_res = lockFilePut(&listlockFileGets, path+strlen(PATH_NAME));
	
	if (pthread_rwlock_wrlock(lock_res) != 0) {
       	error("reader_thread: pthread_rwlock_rdlock error");
      		exit(__LINE__);
    	}
	
	//Scrivo il contenuto dei pacchetti nel file
	for(int i=0; success == 1 && i < numPkt; i++){			
		if(fwrite(pktRcvData[i].data, 1, pktRcvData[i].dataDim, file) <= 0) success = 0;
	}	
	
	if (pthread_rwlock_unlock(lock_res) != 0) {
       	error("reader_thread: pthread_rwlock_rdlock error");
      		exit(__LINE__);
    	}

	//Preparazione messaggio con l'esito dell'operazione	
	if(success){
		prepare_packets(&pktToSend, CNFMSG, strlen(CNFMSG), nextSeqNum);
	}else{
		prepare_packets(&pktToSend, ERRMSG, strlen(ERRMSG), nextSeqNum);
	}
	
	//Invio l'esito dell'operazione secondo il protocollo
	sendto_reliable(sockThread, &addressClient, pktToSend, 1, rto);

	if(fclose(file) == EOF) error("fclose");
	
	free(pktRcvData);
}

/*
Thread creati appositamente per la gestione delle richieste del client. Ogni qual volta un client si connette viene creato un thread che gestirà quel client.
*/
void* thread_job(void* addr){

	int nextSeqNum = 1;
	int lastSeqNumAckedRcv = 1;
	int fd_ready;
	int sockThread;
	long temp;

	char fileName[MAXNAME];
	char path[136] = PATH_NAME;

	struct pktRequest pktRcvRequest;
	struct packet* pktToSend;
	struct sockaddr_in servaddrT;
	struct sockaddr_in addressClient;
	struct timeval time_close;
	struct packet pktForClose;
	struct packet pktCloseAck;
	struct timeval time_request;

	socklen_t len; 	
	fd_set descriptor;

	suseconds_t rto = VALUE_RTO;
	
	addressClient = *((struct sockaddr_in*)(addr));

	memset(&pkt, 0, sizeof(pkt));

	memset(&servaddrT, 0, sizeof(servaddrT)); 
	
	servaddrT.sin_family = AF_INET; 
	servaddrT.sin_addr.s_addr = temp; 
	servaddrT.sin_port = htons(port); 	
	
	printf("Il thread %ld e' nato, gestisce la connesione sulla porta %d\n\n", pthread_self(), ntohs(servaddrT.sin_port));

	//Viene creata la socket per il thread dedicato che verrà utilizzata per la comunicazione con il client 
	if((sockThread = socket(AF_INET, SOCK_DGRAM, 0)) < 0){ 
		perror("socket creation failed"); 
		exit(EXIT_FAILURE); 
	} 
	
	//Viene assegnato l'indirizzo alla socket dedicata
	if(bind(sockThread, (const struct sockaddr *)&servaddrT, sizeof(servaddrT)) < 0 ){ 
		
		perror("bind fallita"); 
		exit(EXIT_FAILURE); 
	} 

	while(1){
	
		//Vengono settate le informazioni per far si che il client, dopo un periodo di tempo che non effettua richiesta al server, invia un pacchetto di FIN
		memset(&time_request, 0, sizeof(struct timeval));
		time_request.tv_sec = 60;
		time_request.tv_usec = 0;

		FD_ZERO(&descriptor);
		FD_SET(sockThread, &descriptor);

		fd_ready = select(sockThread+1, &descriptor , NULL , NULL, &time_request);

		if(fd_ready == 0){
		
			//Caso in cui il timer è scaduto
			threads[ntohs(servaddrT.sin_port) - FIRSTPORT]  = 0;

			//Viene preparato il pacchetto di FIN
			memset(&pktForClose, 0 , sizeof(struct packet));
			pktForClose.fin = 1;
			pktForClose.sequenceNum = nextSeqNum;

			//Invio del pacchetto di FIN al client
			if(sendto(sockThread, &pktForClose, sizeof(pktForClose), 0, (const struct sockaddr *) &addressClient, sizeof(addressClient)) < 0) error("sendto thread job");

			printf("\nIl client connesso alla porta %d si è disconnesso\n" , ntohs(servaddrT.sin_port));
			
			close(sockThread);
			pthread_exit(NULL);
		}else if(fd_ready == -1){
			error("select thread job");
		}
		
		//Il server si mette in attesa di richieste
		len = sizeof(addressClient);
		if(recvfrom(sockThread, &pktRcvRequest, sizeof(pktRcvRequest), 0, ( struct sockaddr *) &addressClient, &len) < 0) error("recvfrom threadjob");
				
				
		if(pktRcvRequest.fin == 1){

			//Caso in cui ho ricevuto una richiesta di chiusura
			printf("\nIl client connesso alla porta %d ha mandato segmento di fin\n" ,ntohs(servaddrT.sin_port));

			while(1){
			
				//Preparo il pacchetto di FIN
				memset(&pktForClose, 0 , sizeof(struct packet));
				memset(&pktCloseAck, 0 , sizeof(struct packet));
				pktForClose.sequenceNum = nextSeqNum;
				pktForClose.fin = 1;
				
				//Invio pacchetto FIN
				if(sendto(sockThread, &pktForClose, sizeof(pktForClose), 0, (const struct sockaddr *) &addressClient, sizeof(addressClient)) < 0)
								error("sendto");

				//Inizializzo le informazione per l'RTO
				FD_ZERO(&descriptor);
				FD_SET(sockThread, &descriptor);
				time_close.tv_sec = 0;
				time_close.tv_usec = 100000;
			
				fd_ready = select(sockThread+1, &descriptor , NULL , NULL, &time_close);
				if(fd_ready  == 0){
				
				//Timer scaduto devo riinviare
				#ifdef TEST
				printf("\nil timer e' scaduto devo riinviare\n");
				#endif

				}else if(fd_ready == 1){

					//Caso in cui ho ricevuto ACK del FIN e posso chiudere connessione
					if(recvfrom(sockThread, &pktCloseAck, sizeof(pktCloseAck), 0, ( struct sockaddr *) &addressClient, &len) < 0) error("errore recvfrom");
					
					#ifdef TEST
					printf("\nHo ricevuto ack del fin \n");
					#endif 
					
					printf("\nIl client connesso alla porta %d si è disconnesso\n" , ntohs(servaddrT.sin_port));
					
					//Vengono liberate risorse del thread e socket
					threads[ntohs(servaddrT.sin_port) - FIRSTPORT] = 0 ;
					close(sockThread);
					sockThread = -1;

					pthread_exit(NULL);
				}else{
					error("fd_ready select");
				}
			}
		}
		
		if(strcmp(pktRcvRequest.request , "list") == 0 ){
			//Esegue comando list , bisgona girare il risultato al client
			memset(pktRcvRequest.request , 0 , 4);
			list(addressClient, sockThread, pktToSend, &nextSeqNum, &rto);

		}else if(strncmp(pktRcvRequest.request, "get ", 4) == 0){
			//Copia su fileName l'effettivo nome del file
			strcpy(fileName , pktRcvRequest.request + 4);

			memset(pktRcvRequest.request , 0 , strlen(pktRcvRequest.request));				
			get(fileName , addressClient, sockThread , pktToSend, &nextSeqNum, &rto);

		}else if(strncmp(pktRcvRequest.request, "put ", 4) == 0){
			//Copia su fileName l'effettivo nome del file
			strcpy(fileName , pktRcvRequest.request + 4);
			strcat(path , fileName);
			
			memset(pktRcvRequest.request , 0 , strlen(pktRcvRequest.request));
			put(path , addressClient, sockThread, pktToSend, &nextSeqNum, &lastSeqNumAckedRcv, &rto);
			
		}
		//Viene pulito il path eliminando il nome del file caricato precedentemente
		memset(path+8, 0, strlen(fileName));
	}
}


int main(int argc, char* argv[]) { 
	
	int sockfd;

	bool porta_disponibile = 0;

	struct packet* pktSender;
	struct sockaddr_in listeningSocket;
	struct sockaddr_in addressSender;

	socklen_t len; 
	
	DIR *d;
	
	struct dirent *dir; 
	
	//Viene verificato il corretto lancio dell'applicazione
	if (argc != 2) {
		exception("utilizzo: ./server [dimensione della finestra]" , -1);
		exit(EXIT_FAILURE);
	}else{
		if(atoi(argv[1]) < 2) {
			exception("La dimensione della finestra inserita non è valida , inserisci un valore > 2" , -1);
			exit(EXIT_FAILURE);
		}
	}
	
	//Viene settata la dimensione della finestra
	setWindowSize(atoi(argv[1]));
	
	//Viene creata la socket
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("socket creation failed"); 
		exit(EXIT_FAILURE); 
	}
	
	//Vengono settate le informazioni per la bind
	memset(&listeningSocket, 0, sizeof(listeningSocket)); 
	listeningSocket.sin_family = AF_INET; 
	listeningSocket.sin_addr.s_addr = htonl(INADDR_ANY); 
	listeningSocket.sin_port = htons(PORT); 
	
	if(bind(sockfd, (const struct sockaddr *)&listeningSocket, sizeof(listeningSocket)) < 0){ 
		
		perror("bind fallita"); 
		exit(EXIT_FAILURE); 
	} 

	len = sizeof(addressSender);

	memset(&pkt, 0, sizeof(pkt));
	
	
	//All'avvio il server prepara la lista collegata per mantenere le risorse di atomicità in scrittura di tutti i files presenti attualmente nel server
	if((d = opendir("./files")) == NULL){
	
		error("opendir");
	}else{
		
		bool first = 1;

		rewinddir(d);
		while((dir = readdir(d)) != NULL){
			if(dir->d_type == DT_REG){
				if(first){
					
					insertHead(&listlockFileGets, dir->d_name);
					first = 0;
				}else{
					
					insertFileLock(&listlockFileGets, dir->d_name);
				}
			}
		}
		if(closedir(d) != 0) error("close dir");	
	}	
	
	//Inizializzo il mutex in accesso alla lista quando si effettuano put
	pthread_mutex_init(&mutexList, NULL);
	
	//mutexList = PTHREAD_MUTEX_INITIALIZER; 
	
	while(1){
	
		/* Handshaking a 3 vie */
		
		//In attesa del pacchetto di SYN
		if(recvfrom(sockfd, &pktRcv, sizeof(pktRcv), 0, ( struct sockaddr *) &addressSender, &len) < 0) error("errore recvfrom");
		
		if(pktRcv.syn == 1){

			//Preparo pacchetto di ACK per il SYN del client
			pktSender = (struct packet*) prepare_mem(sizeof(struct packet), "malloc pktSender");

			pktSender[0].validAck = 1;
			pktSender[0].ackNum = atoi(argv[1])/*pktRcv.sequenceNum + 1*/; 
			pktSender[0].sequenceNum = 0;

			for(int i = FIRSTPORT ; i <= MAXPORT ; i++){
				if (threads[i - FIRSTPORT] == 0){ 
					porta_disponibile = 1;
					port = i;
					break;
				}
			}

			if(porta_disponibile == 1){
				
				//Caso in cui è stata trovata una porta dispobile
				//Preparo pacchetto di SYN
				pktSender[0].syn = 1; 
	
				//Setto come dati del pacchetto la porta e la dimensione della finestra con cui dovrà comunicare il client
				sprintf(pktSender[0].data , "%d",  htons(port));
	
				//Invito il pacchetto di SYN-ACK
				if(sendto(sockfd, &pktSender[0], sizeof(pktSender[0]), 0, (const struct sockaddr *) &addressSender, len) < 0)//invio syn-ack
									error("sendto");

				//In attesa dell'ACK da parte del client
				if(recvfrom(sockfd, &pktRcv, sizeof(pktRcv), 0, ( struct sockaddr *) &addressSender, &len) < 0) error("errore recvfrom");
				
				//Se ricevo l'ACK corretto
				if(pktRcv.validAck == 1 && pktRcv.ackNum == pktSender[0].sequenceNum + 1){
				
					//Viene creato un thread dedicato per la gestione delle richieste del client
					if(pthread_create(&threads[port - FIRSTPORT], NULL, thread_job, (void*) &addressSender) != 0) error("pthread_create");
				}
			}else{
				
				//La sincronizzazione non può avvenire perchè il server non ha porte disponibili
				pktSender[0].syn = 0; 

				if(sendto(sockfd, &pktSender[0], sizeof(pktSender[0]), 0, (const struct sockaddr *) &addressSender, len) < 0)//invio syn
									error("sendto");
			}
			
			free(pktSender);
		}
		porta_disponibile = 0;
	}
	return 0; 
} 

