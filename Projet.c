#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/sem.h>
#include <string.h>

#define MUTEX_FileAttenteDecollageP 0      //Mutex pour les 4 files d'attente et les 2 pistes
#define MUTEX_FileAttenteAtterrissageP 1
#define MUTEX_FileAttenteDecollageC 2
#define MUTEX_FileAttenteAtterrissageC 3
#define MUTEX_PistePassagers 4
#define MUTEX_PisteCargo 5

#define SEM_PistePassagersDecollage 0      //Sémaphores pour les pistes (2 par pistes)
#define SEM_PistePassagersAtterrissage 1
#define SEM_PisteCargoDecollage 2
#define SEM_PisteCargoAtterrissage 3
#define SKEY (key_t) IPC_PRIVATE

void *avionPassagers(void *AvionsID);      //Déclaration pour satisfaire le compilateur et éviter le warning "conflicting types"
void *avionCargo(void *AvionsIDc);
void *tourDeControleCargo();
void libereSem(int semid);
void V(int semnum);
void P(int semnum);

int PistePassagers = 0; 
int PisteCargo = 0; 			 		   //Les pistes sont égales à 1 si elles sont utilisées, permet de savoir si il y a eu crash ou non
int FileAttenteDecollageP = 0;
int FileAttenteDecollageC = 0;
int FileAttenteAtterrissageP = 0;
int FileAttenteAtterrissageC = 0;

int Meteo = 0;      			 // = 0 si ensoleillé, 4 si orageux, 8 si pluvieux, 10 si nuageux. De tel nombres permettent de réduire la taille de la procédure de test dans les thread avions

int Compteur = 1;    			 //Identificateur du numéro d'avions passagers
int CompteurC = 1;				 //Identificateur du numéro d'avions cargo
int keroseneP[50]; 			     //Tableau de valeur pour stocker les jauges de kerosene des avions passagers (cargo en dessous), et gérer les priorités d'atterrissage 
int keroseneC[50];					
	
pthread_mutex_t mutex[5];  		 //Mutex init
int sem_id;                		 //Sémaphores pour que l'accès aux pistes soit géré par la tour de controle.


/****************Fonction génération de destination ************/
/*   - Génère une destination et retourne le résultat 		   */
/***************************************************************/
void destinationGenerator(char **destGen)
{
	int Destination = rand()%10;		//Utilisation de pointeur car return ne renvoi que des int, impossible de lui faire renvoyer un char*
	switch (Destination)
	{
		case 0:
			*destGen = "Suède";	
			break;
		case 1:
			*destGen = "Finlande";	
			break;
		case 2:
			*destGen = "Estonie";	
			break;
		case 3:
			*destGen = "Lituanie";	
			break;
		case 4:
			*destGen = "Allemagne";	
			break;
		case 5:
			*destGen = "Italie";	
			break;
		case 6:
			*destGen = "Autriche";	
			break;
		case 7:
			*destGen = "Slovénie";	
			break;
		case 8:
			*destGen = "Espagne";	
			break;
		case 9:
			*destGen = "Bulgarie";	
			break;
	}
}

/****************Fonction de gestion piste passagers************/
/*   - Communique avec les avions pour atterrissages/décollages*/
/*	 - Nettoyage de la piste                                   */
/*   - Annonce météo                                           */
/***************************************************************/
void tourDeControle()
{
	printf("SkyWatcher à tous les avions : ANNONCE METEO DU JOUR\n\n");
	srand(time(NULL));
	Meteo = rand()%4+1;
	switch (Meteo)
	{
		case 1:
			Meteo = 4;
			printf("!! Le temps est orageux, risque de crash important !! \n\n");
			break;
		case 2:
			Meteo = 8;
			printf("Le temps est pluvieux, risque de crash modéré. \n\n");
			break;
		case 3:
			Meteo = 10;
			printf("Le temps est nuageux, risque de crash faible. \n\n");
			break;
		case 4:
			Meteo = 100;
			printf("Le soleil brille de mille feux, mettez vos lunettes ! \n\n");
			break;
	}

	pthread_t TDCCargo; 		     	 //Lance le thread pour la gestion des Cargo
	pthread_create(&TDCCargo, NULL, tourDeControleCargo, NULL);

	int nbgen = rand()%3+1;               //Génère 1 à 3 avions passagers sur la piste passagers 
	pthread_t thread[nbgen];			  //Thread avions init : Mode détaché car pas de pthreadjoin
	pthread_attr_t thread_attr;

	pthread_attr_init(&thread_attr);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);

						  
	long AvionsID;                       				 //Type long pour éviter le Warning "different size" lors du passage de int à void
	for (int i = 0; i < nbgen; i++)
	{
		AvionsID = Compteur;             				 //Récupère son ID à sa création
		Compteur ++;

		char *destGen; 
		destinationGenerator(&destGen);          		 //Affecte une destination
		printf("Avion n° %ld, vous irez en %s !\n",AvionsID, destGen);

		pthread_create(&thread[i],&thread_attr, avionPassagers, (void *) AvionsID);
	} 

	sleep(2);
	
	while(1)
	{
		sleep(2);
		pthread_mutex_lock(&mutex[MUTEX_FileAttenteAtterrissageP]); //Mutex pour le test if
		if(FileAttenteAtterrissageP == 0)       			  //Si la file d'attente atterrissage est vide
		{
			pthread_mutex_unlock(&mutex[MUTEX_FileAttenteAtterrissageP]);
			pthread_mutex_lock(&mutex[MUTEX_FileAttenteDecollageP]);
			if(FileAttenteDecollageP != 0)     				  //Si la File d'attente décollage n'est pas vide
			{
		   		printf("\nSkyWatcher : Vous pouvez décoller\n");
		   		pthread_mutex_unlock(&mutex[MUTEX_FileAttenteDecollageP]);
		   		V(SEM_PistePassagersDecollage);  		      //Réveille un avion en attente de décollage
		    }
			else  //FileAttenteDecollageP == 0				  //Si la file d'attente décollage est vide
			{
				pthread_mutex_unlock(&mutex[MUTEX_FileAttenteDecollageP]);
				printf("SkyWatcher : Allez, pause café !\n");
				sleep(2);

				nbgen = rand()%2+1;
				for (int i = 0; i < nbgen; i++)     		 //Génère 1 à 3 avions sur la piste de décollage, de la même façon que précédemment
				{	
					AvionsID = Compteur;              
					Compteur ++;
					pthread_create(&thread[i],NULL, avionPassagers, (void *) AvionsID);
				} 
			}

		}
		else  //FileAttenteAtterrisageP != 0                //Si la file d'attente atterrissage n'est pas vide
		{
			printf("\nSkyWatcher : Vous pouvez atterrir\n");
			pthread_mutex_unlock(&mutex[MUTEX_FileAttenteAtterrissageP]);
			V(SEM_PistePassagersAtterrissage); 				//Fait atterrir un avion
	        sleep(1);										//Laisse le temps au thread qui vient de se réveiller de récupérer le mutex piste passagers
	        pthread_mutex_lock(&mutex[MUTEX_PistePassagers]);
	        if (PistePassagers == 1)						//Vérifie si l'avion s'est crashé, et nettoie la piste en conséquence
        	{
        		printf("SkyWatcher à StationDeNettoyage : il y a des débris sur la piste Passagers, réglez moi ça tout de suite !\n");
        		printf("StationDeNettoyage nettoie la piste passagers, veuillez patienter.\n");
        		sleep(2);
        		PistePassagers = 0;
        		printf("La piste passagers est dégagée, le traffic peut reprendre\n");
        		pthread_mutex_unlock(&mutex[MUTEX_PistePassagers]);
        	}	
	        else 
	        {
        		pthread_mutex_unlock(&mutex[MUTEX_PistePassagers]);
	        }
		}
	}
	
}


/****************Fonction de gestion piste cargo****************/
/*   - Communique avec les avions pour atterrissages/décollages*/
/*	 - Nettoyage de la piste                                   */
/***************************************************************/
void *tourDeControleCargo()
{
	int nbgenC = rand()%3+1;              //Génère 1 à 3 avions passagers sur la piste passagers (Mode détaché aussi)
	pthread_t threadC[nbgenC];
	pthread_attr_t thread_attrC;

	pthread_attr_init(&thread_attrC);
	pthread_attr_setdetachstate(&thread_attrC, PTHREAD_CREATE_DETACHED);
	
	long AvionsIDc;                       //type long pour éviter le Warning different size lors du passage de int à void
	for (int j = 0; j < nbgenC; j++)
	{
		AvionsIDc = CompteurC;              //Récupère son ID à sa création
		CompteurC ++;

		char *destGen; 
		destinationGenerator(&destGen);          		 //Affecte une destination
		printf("Cargo n° %ld, vous irez en %s !\n",AvionsIDc, destGen);

		pthread_create(&threadC[j],&thread_attrC, avionCargo, (void *) AvionsIDc);
	} 

	sleep(2);
	
	while(1)
	{
		sleep(2);
		pthread_mutex_lock(&mutex[MUTEX_FileAttenteAtterrissageC]); //Mutex pour le test if
		if(FileAttenteAtterrissageC == 0)       			  //Si la file d'attente atterrissage est vide
		{
			pthread_mutex_unlock(&mutex[MUTEX_FileAttenteAtterrissageC]);
			pthread_mutex_lock(&mutex[MUTEX_FileAttenteDecollageC]);
			if(FileAttenteDecollageC != 0)     				  //Si la File d'attente décollage n'est pas vide
			{
		   		printf("\nSkyWatcherCargo : Vous pouvez décoller\n");
		   		pthread_mutex_unlock(&mutex[MUTEX_FileAttenteDecollageC]);
		   		V(SEM_PisteCargoDecollage);  		      //Réveille un avion en attente de décollage
		    }
			else  //FileAttenteDecollageC == 0				  //Si la file d'attente décollage est vide
			{
				pthread_mutex_unlock(&mutex[MUTEX_FileAttenteDecollageC]);
				printf("\nSkyWatcherCargo se repose un peu, le traffic est inexistant\n");
				sleep(2);

				nbgenC = rand()%2+1;
				for (int j = 0; j < nbgenC; j++)     		 //Génère 1 à 3 avions sur la piste de décollage, de la même façon que précédemment
				{	
					AvionsIDc = CompteurC;              
					CompteurC ++;
					pthread_create(&threadC[j],NULL, avionCargo, (void *) AvionsIDc);
				} 
			}

		}
		else  //FileAttenteAtterrisageC != 0                //Si la file d'attente atterrissage n'est pas vide
		{
			printf("\nSkyWatcherCargo : Vous pouvez atterrir\n");
			pthread_mutex_unlock(&mutex[MUTEX_FileAttenteAtterrissageC]);
			V(SEM_PisteCargoAtterrissage); 				//Fait atterrir un avion
	        sleep(1);										//Laisse le temps au thread qui vient de se réveiller de récupérer le mutex piste passagers
	        pthread_mutex_lock(&mutex[MUTEX_PisteCargo]);
	        if (PisteCargo == 1)						//Vérifie si l'avion s'est crashé, et nettoie la piste en conséquence
        	{
        		printf("SkyWatcherCargo à StationDeNettoyage : il y a des débris sur la piste Cargo, nous avons besoin d'un coup de balai !\n");
        		printf("StationDeNettoyage nettoie la piste cargo, veuillez patienter.\n");
        		sleep(2);
        		PisteCargo = 0;
        		printf("La piste cargo est dégagée, le traffic peut reprendre\n");
        		pthread_mutex_unlock(&mutex[MUTEX_PisteCargo]);
        	}	
	        else 
	        {
        		pthread_mutex_unlock(&mutex[MUTEX_PisteCargo]);
	        }
		}
	}

}

/***************Thread vie d'un avion passager******************/
/*   - Création de l'avion (Au sol)                            */
/*   - Demande le décollage, empreinte la piste passagers      */
/*   - Demande l'atterrissage, empreinte la piste passagers    */
/***************************************************************/
void *avionPassagers(void *AvionsID)
{
	sleep(2);
	int ID = (long) AvionsID;
	keroseneP[ID] = 100;

	pthread_mutex_lock(&mutex[MUTEX_FileAttenteDecollageP]);  		 //Incrémente la file d'attente au décollage
	FileAttenteDecollageP ++;
	printf("Avion n° %d à SkyWatcher : Prêt au décollage\n", ID);
	pthread_mutex_unlock(&mutex[MUTEX_FileAttenteDecollageP]);

	P(SEM_PistePassagersDecollage);         						 //S'endort en attendant l'autorisation de la tour
	pthread_mutex_lock(&mutex[MUTEX_PistePassagers]);				 //Empreinte la piste

	
	pthread_mutex_lock(&mutex[MUTEX_FileAttenteDecollageP]);  		 //Décrémente la file d'attente décollage
	FileAttenteDecollageP --;
	printf("Avion n° %d décolle, attachez vos ceintures !\n", ID);
	pthread_mutex_unlock(&mutex[MUTEX_FileAttenteDecollageP]);												

	pthread_mutex_unlock(&mutex[MUTEX_PistePassagers]);
	printf("Avion n° %d à décollé, il s'envole vers d'autres cieeeeeux\n", ID);   //L'avion a décollé

	int tpsVol = rand()%10+3;               //S'endort de 3 à 10s, simule un temps de vol
	sleep(tpsVol);
	keroseneP[ID] = 100 - (tpsVol * 8);     //Maj du réservoir de l'avion

	pthread_mutex_lock(&mutex[MUTEX_FileAttenteAtterrissageP]);					  //Incrémente la file d'attente atterrissage
	FileAttenteAtterrissageP ++;
	char pourcent = '%';
	printf("Avion n° %d à SkyWatcher : demande autorisation d'atterrir. Ma jauge de kerosene est à %d%c\n", ID, keroseneP[ID], pourcent);
	pthread_mutex_unlock(&mutex[MUTEX_FileAttenteAtterrissageP]);

	P(SEM_PistePassagersAtterrissage);								 			  //S'endort en attendant l'autorisation de la tour
				
	pthread_mutex_lock(&mutex[MUTEX_PistePassagers]);
	pthread_mutex_lock(&mutex[MUTEX_FileAttenteAtterrissageP]);					  //Décrémente la file d'attente atterrissage
	FileAttenteAtterrissageP --;
	pthread_mutex_unlock(&mutex[MUTEX_FileAttenteAtterrissageP]);

	PistePassagers = 1;
	printf("Avion n° %d à SkyWatcher, je débute la procédure d'atterrissage\n",ID);
	int testCrash = rand()%Meteo;												  //Test de crash, le risque de crash diffère selon la météo
	if (testCrash == 0)
	{
		printf("Avion n° %d à SkyWatcher, je perds le contrôle ! AAAAAAAAAAAAAAAAHHHHHHHHHHHHHHHHHHHHHHH\n", ID);
		printf(" *BOUM* \n");
		printf("Avion n° %d n'a fait qu'un avec le sol\n", ID);
		pthread_mutex_unlock(&mutex[MUTEX_PistePassagers]);					      //Libère la piste qui vaut 1, permet de lancer la procédure de nettoyage depuis la tour de contrôle
	}
	else  //TestCrash different de 0
	{
		printf("Avion n° %d à atterrit avec succès. Les passagers débarquent\n", ID);
		PistePassagers = 0;
		pthread_mutex_unlock(&mutex[MUTEX_PistePassagers]);
	}    
	
	pthread_exit(NULL);
}


/**************Thread vie d'un avion cargo**********************/
/*   - Création de l'avion (Au sol)                            */
/*   - Demande le décollage, empreinte la piste Cargo          */
/*   - Demande l'atterrissage, empreinte la piste Cargo        */
/***************************************************************/
void *avionCargo(void *AvionsIDc)
{
	sleep(2);
	int IDc = (long) AvionsIDc;
	keroseneC[IDc] = 100;

	pthread_mutex_lock(&mutex[MUTEX_FileAttenteDecollageC]);  		 //Incrémente la file d'attente au décollage
	FileAttenteDecollageC ++;
	printf("Cargo n° %d à SkyWatcherCargo : Prêt au décollage\n", IDc);
	pthread_mutex_unlock(&mutex[MUTEX_FileAttenteDecollageC]);

	P(SEM_PisteCargoDecollage);         						 //S'endort en attendant l'autorisation de la tour
	pthread_mutex_lock(&mutex[MUTEX_PisteCargo]);				 //Empreinte la piste

	
	pthread_mutex_lock(&mutex[MUTEX_FileAttenteDecollageC]);  		 //Décrémente la file d'attente décollage
	FileAttenteDecollageC --;
	printf("Cargo n° %d décolle, vers l'infini et au-delà !\n", IDc);
	pthread_mutex_unlock(&mutex[MUTEX_FileAttenteDecollageC]);												

	pthread_mutex_unlock(&mutex[MUTEX_PisteCargo]);
	printf("Cargo n° %d à décollé, les pilotes nous font coucou !\n", IDc);   //L'avion a décollé

	int tpsVolc = rand()%10+3;               //S'endort de 3 à 10s, simule un temps de vol
	sleep(tpsVolc);
	keroseneC[IDc] = 100 - (tpsVolc * 6);     //Maj du réservoir de l'avion

	pthread_mutex_lock(&mutex[MUTEX_FileAttenteAtterrissageC]);					  //Incrémente la file d'attente atterrissage
	FileAttenteAtterrissageC ++;
	char pourcentc = '%';
	printf("Cargo n° %d à SkyWatcherCargo : demande autorisation d'atterrir. Ma jauge de kerosene est à %d%c\n", IDc, keroseneC[IDc], pourcentc);
	pthread_mutex_unlock(&mutex[MUTEX_FileAttenteAtterrissageC]);

	P(SEM_PisteCargoAtterrissage);								 			  //S'endort en attendant l'autorisation de la tour
				
	pthread_mutex_lock(&mutex[MUTEX_PisteCargo]);
	pthread_mutex_lock(&mutex[MUTEX_FileAttenteAtterrissageC]);					  //Décrémente la file d'attente atterrissage
	FileAttenteAtterrissageC --;
	pthread_mutex_unlock(&mutex[MUTEX_FileAttenteAtterrissageC]);

	PisteCargo = 1;
	printf("Cargo n° %d à SkyWatcherCargo, je débute la procédure d'atterrissage\n",IDc);
	int testCrashC = rand()%Meteo;												  //Test de crash, le risque de crash diffère selon la météo
	if (testCrashC == 1)
	{
		printf("Cargo n° %d à SkyWatcherCargo, je crois que je me suis trompé de bouton\n", IDc);
		printf(" *BOUM* \n");
		printf("Cargo n° %d s'était effectivement trompé de bouton\n\n", IDc);
		pthread_mutex_unlock(&mutex[MUTEX_PisteCargo]);					      //Libère la piste qui vaut 1, permet de lancer la procédure de nettoyage depuis la tour de contrôle
	}
	else  //TestCrashC different de 1
	{
		printf("Cargo n° %d à atterrit avec succès. Nous vidons la soute\n\n", IDc);
		PisteCargo = 0;
		pthread_mutex_unlock(&mutex[MUTEX_PisteCargo]);
	}    
	
	pthread_exit(NULL);
}

/******** Fonction qui ferme l'Aéroport à Ctrl + C *************/
void traitantSIGINT(int num)
{
	libereSem(sem_id);    //Fermeture des sémaphores
	printf(	"\n==================================================\n"
			"       	   FERMETURE DE L'AEROPORT	           \n"
			"==================================================\n" );      
	exit(0);
}

/************SEMAPHORES STRUCT P*******************/
void P(int semnum) 
{
	struct sembuf sem_oper_P;
	sem_oper_P.sem_num = semnum;        
	sem_oper_P.sem_op  = -1 ;
	sem_oper_P.sem_flg = 0 ;

	if(semop(sem_id, &sem_oper_P, 1) < 0)
	{
		perror("Erreur Semop P\n");
	}
}

/************SEMAPHORES STRUCT V*******************/
void V(int semnum) 
{
	struct sembuf sem_oper_V;
	sem_oper_V.sem_num = semnum;
	sem_oper_V.sem_op  = 1 ;
	sem_oper_V.sem_flg  = 0 ;

	if(semop(sem_id, &sem_oper_V, 1) < 0)  
	{
		perror("Erreur Semop V\n");
	}
}

/****************LIBERE SEMAPHORES****************/
void libereSem(int semid)
{
	semctl(semid, 0, IPC_RMID);            //Supprime la liste des sémaphores
}

/****************INITALISE SEMAPHORES************/
int initSem(key_t semkey)
{
	int status = 0;  
	int semid_init;  
	
	union semun {
		int val;
		struct semid_ds *stat;
		short * array;
	} ctl_arg;

	semid_init = semget(semkey, 4, IPC_CREAT | 0600);   	  //Créé tab de 2 sémaphores
	short array[4] = {0,0,0,0};                               //Valeur des sémaphores à l'initialisation
	ctl_arg.array = array;							
	status = semctl(semid_init, 0, SETALL, ctl_arg);        

	if (semid_init == -1 || status == -1)
	{
		perror("Erreur initSem");
		return(-1);
	}
	else return(semid_init);
}

/**************** MAIN **************************/
int main(int argc, char *argv[])
{
	printf(	"==================================================\n"
			"              OUVERTURE DE L'AEROPORT     	       \n"
			"==================================================\n" );

	sem_id = initSem(SKEY);         //Initialise la liste de sémaphores
	signal(SIGINT, traitantSIGINT); //Arme le signal en cas d'arrêt
	tourDeControle();
}