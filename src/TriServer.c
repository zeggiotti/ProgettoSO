#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include "data.h"

void printError(const char *);
void init_data();
void removeIPCs();
void signal_handler(int);
void set_sig_handlers();
void p(int);
void v(int);

// Id del seg. di memoria condivisa che contiene i dati della partita.
int lobbyDataId = 0;

// Indirizzo di memoria condivisa che contiene i dati della partita.
struct lobby_data *info = NULL;

// Indirizzo di memoria condivisa che contiene la matrice di gioco.
char **board = NULL;

int main(int argc, char *argv[]){

    if(argc < 4) {
        // Richiesta mal formata al server.
        printf("%s", HELP_MSG);
        exit(0);
    } else {

        set_sig_handlers();

        init_data(argv);

        // Ad ogni segnale che fa riprendere l'esecuzione (SIGUSR1 dei client) si controlla se
        // stanno partecipando due giocatori. Se così non è si aspetta ancora.
        int gameIsReady = 0;
        do {
            pause();
            
            p(INFO_SEM);
                
            if(info->client_pid[0] != 0 && info->client_pid[1] != 0)
                gameIsReady = 1;

            v(INFO_SEM);
        } while(!gameIsReady);

        printf("%s\n", GAME_STARTING);

        p(INFO_SEM);     

        // La partita ha inizio. Lo si comunica ai client facendo riprendere la loro esecuzione.
        if(kill(info->client_pid[0], SIGUSR1) == -1)
            printError(SIGUSR1_SEND_ERR);

        if(kill(info->client_pid[1], SIGUSR1) == -1)
            printError(SIGUSR1_SEND_ERR);

        info->game_started = 1;

        v(INFO_SEM);

        // Per ora: si rimane bloccati al posto di giocare.
        pause();

        removeIPCs();
    }

}

/**
 * Imposta gli handler dei segnali da catturare.
*/
void set_sig_handlers(){
    if(signal(SIGINT, signal_handler) == SIG_ERR){
        printError(SIGINT_HANDLER_ERR);
    }

    if(signal(SIGUSR1, signal_handler) == SIG_ERR){
        printError(SIGUSR1_HANDLER_ERR);
    }

    if(signal(SIGUSR2, signal_handler) == SIG_ERR){
        printError(SIGUSR2_HANDLER_ERR);
    }
}

/**
 * Procedura P Wait.
*/
void p(int semnum){
    struct sembuf p;
    p.sem_num = semnum;
    p.sem_op = -1;
    p.sem_flg = 0;

    if(semop(info->semaphores, &p, 1) == -1)
        printError(P_ERR);
}

/**
 * Procedura V Signal.
*/
void v(int semnum){
    struct sembuf v;
    v.sem_num = semnum;
    v.sem_op = 1;
    v.sem_flg = 0;

    if(semop(info->semaphores, &v, 1) == -1)
        printError(V_ERR);
}

/**
 * Inizializza i dati necessari a giocare, ovvero i dati riguardanti client, server e la generale gestione della partita (lobby).
 * TODO: Organizzare meglio l'ordine di esecuzione per evitare race condition su info.
 * TODO: Migliore controllo sui dati in input.
*/
void init_data(char *argv[]){
    key_t lobbyShmKey = ftok(PATH_TO_FILE, FTOK_KEY);
    if(lobbyShmKey == -1)
        printError(FTOK_ERR);

    lobbyDataId = shmget(lobbyShmKey, sizeof(struct lobby_data), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    if(lobbyDataId == -1){
        printError(GAME_EXISTING_ERR);
    }

    // Contiene le informazioni della partita.
    info = shmat(lobbyDataId, NULL, 0);
    if(info == (void *)-1){
        printError(SHMAT_ERR);
    }

    info->semaphores = semget(IPC_PRIVATE, 2, S_IRUSR | S_IWUSR);
    if(info->semaphores == -1){
        printError(SEM_ERR);
    }

    short values[] = {1, 1};
    union semun arg;
    arg.array = values;
    if(semctl(info->semaphores, 0, SETALL, arg) == -1){
        printError(SEM_ERR);
    }

    p(INFO_SEM);

    int board_shmid = shmget(IPC_CREAT, 9 * sizeof(char), IPC_CREAT | S_IRUSR | S_IWUSR);
    if(board_shmid == -1){
        printError(BOARD_SHM_ERR);
    }

    info->server_pid = getpid();
    info->client_pid[0] = 0;
    info->client_pid[1] = 0;

    info->num_clients = 0;
    info->timeout = atoi(argv[1]);
    info->signs[0] = argv[2][0];
    info->signs[1] = argv[3][0];

    info->game_started = 0;

    info->board_shmid = board_shmid;
    
    board = shmat(board_shmid, NULL, 0);
    if(board == (void *) -1)
        printError(SHMAT_ERR);

    v(INFO_SEM);
}

/**
 * Stampa un messaggio d'errore. Prima di terminare, rimuove tutti gli IPC creati.
*/
void printError(const char *msg){
    printf("%s\n", msg);
    removeIPCs();
    exit(EXIT_FAILURE);
}

/**
 * FAQ: Dovrebbe anche terminare i client??
*/
void removeIPCs(){
    // Rimozione e staccamento di/da shm di lobby e matrice di gioco e semafori.
    if(semctl(info->semaphores, 0, IPC_RMID, 0) == -1){
        printf("%s\n", SEM_DEL_ERR);
    }

    if(board != NULL){
        if(shmdt(board) == -1)
            printf("%s\n", SHMDT_ERR);
    }
    
    if(info->board_shmid != 0){
        if(shmctl(info->board_shmid, IPC_RMID, NULL) == -1){
            printf("%s\n", SHM_DEL_ERR);
        }
    }

    if(info != NULL){
        if(shmdt(info) == -1)
            printf("%s\n", SHMDT_ERR);
    }

    if(lobbyDataId != 0){
        if(shmctl(lobbyDataId, IPC_RMID, NULL) == -1){
            printf("%s\n", SHM_DEL_ERR);
        }
    }
}

/** TODO: Gestione chiusura del terminale. */
void signal_handler(int sig){
    /** TODO: Doppia pressione del Ctrl+C. */
    if(sig == SIGINT) {
        // Pressione di Ctrl+C. Si fanno terminare i client e poi il server termina.
        p(INFO_SEM);

        if(info->client_pid[0] != 0)
            if(kill(info->client_pid[0], SIGTERM) == -1)
                    printError(SIGTERM_SEND_ERR);

        if(info->client_pid[1] != 0)
            if(kill(info->client_pid[1], SIGTERM) == -1)
                printError(SIGTERM_SEND_ERR);

        v(INFO_SEM);

        removeIPCs();
        exit(0);
    } else if (sig == SIGUSR2){
        // Un client ha premuto Ctrl+C. Se la partita è iniziata, l'altro client vince a tavolino. Altrimenti non si
        // controlla nulla: siamo in fase di attesa giocatori, chiunque può entrare o uscire dalla lobby.
        p(INFO_SEM);

        if(info->game_started){
            if(info->client_pid[0] == 0){
                if(info->client_pid[1] != 0)
                    if(kill(info->client_pid[1], SIGTERM) == -1)
                        printError(SIGTERM_SEND_ERR);
            } else if (info->client_pid[1] == 0){
                if(info->client_pid[0] != 0)
                    if(kill(info->client_pid[0], SIGTERM) == -1)
                        printError(SIGTERM_SEND_ERR);
            }
        }

        v(INFO_SEM);
    }
}
