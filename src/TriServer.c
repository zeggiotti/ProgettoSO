#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
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

// Timestamp dell'ultima pressione di Ctrl+C.
int sigint_timestamp = 0;

// Set di segnali ricevibili dal processo.
sigset_t processSet;

int main(int argc, char *argv[]){

    int isTimeoutNumber = 1;
    if(argc > 1){
        for(int i = 0; i < strlen(argv[1]); i++){
            if(argv[1][i] < '0' || argv[1][i] > '9')
                isTimeoutNumber = 0;
        }
    }

    if(argc < 4 || !isTimeoutNumber || strlen(argv[2]) > 1 || strlen(argv[3]) > 1) {
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
        // NB: Il server non termina se uno dei due processi quitta. Questo perché ritorna qui ad aspettare;
        int gameStarted = 0;
        do {
            pause();
            p(INFO_SEM);
            gameStarted = info->game_started;
            v(INFO_SEM);
        } while (gameStarted);

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

    if(signal(SIGHUP, signal_handler) == SIG_ERR){
        printError(SIGHUP_HANDLER_ERR);
    }
}

/**
 * Procedura P Wait.
*/
void p(int semnum){
    sigset_t noInterruptionSet, oldSet;

    sigfillset(&noInterruptionSet);
    sigprocmask(SIG_SETMASK, &noInterruptionSet, &oldSet);

    processSet = oldSet;

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

    sigprocmask(SIG_SETMASK, &processSet, NULL);
}

/**
 * Inizializza i dati necessari a giocare, ovvero i dati riguardanti client, server e la generale gestione della partita (lobby).
*/
void init_data(char *argv[]){
    key_t lobbyShmKey = ftok(PATH_TO_FILE, FTOK_KEY);
    if(lobbyShmKey == -1)
        printError(FTOK_ERR);

    int sems = semget(IPC_PRIVATE, 2, S_IRUSR | S_IWUSR);
    if(sems == -1){
        printError(SEM_ERR);
    }

    short values[] = {1, 1};
    union semun arg;
    arg.array = values;
    if(semctl(sems, 0, SETALL, arg) == -1){
        printError(SEM_ERR);
    }

    // Dopo aver inizializzato i semafori, si esegue una P su essi per inizializzare i dati condivisi.
    // Questo per permettere ai client di vedere la presenza di una partita, ma non accedervi veramente perché in fase
    // di inizializzazione da parte del server.

    sigset_t noInterruptionSet, oldSet;

    sigfillset(&noInterruptionSet);
    sigprocmask(SIG_SETMASK, &noInterruptionSet, &oldSet);

    processSet = oldSet;

    struct sembuf p;
    p.sem_num = 0;
    p.sem_op = -1;
    p.sem_flg = 0;

    if(semop(sems, &p, 1) == -1)
        printError(P_ERR);

    lobbyDataId = shmget(lobbyShmKey, sizeof(struct lobby_data), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    if(lobbyDataId == -1){
        printError(GAME_EXISTING_ERR);
    }

    // Contiene le informazioni della partita.
    info = shmat(lobbyDataId, NULL, 0);
    if(info == (void *)-1){
        printError(SHMAT_ERR);
    }

    info->semaphores = sems;

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

    struct sembuf v;
    v.sem_num = 0;
    v.sem_op = 1;
    v.sem_flg = 0;

    if(semop(sems, &v, 1) == -1)
        printError(V_ERR);

    sigprocmask(SIG_SETMASK, &processSet, NULL);
}

/**
 * Stampa un messaggio d'errore. Prima di terminare, rimuove tutti gli IPC creati.
*/
void printError(const char *msg){
    printf("%s\n", msg);
    removeIPCs();
    exit(EXIT_FAILURE);
}

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

void signal_handler(int sig){
    if(sig == SIGINT || sig == SIGHUP) {

        // Ritorna indietro per scrivere sopra al carattere ^C
        printf("\r");

        int now = time(NULL);
        if(now - sigint_timestamp < MAX_SECONDS || sig == SIGHUP) {

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

        } else {
            sigint_timestamp = now;
            printf("Per terminare l'esecuzione, premere Ctrl+C un'altra volta entro %d secondi.\n", MAX_SECONDS);
        }

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
            info->game_started = 0;
        }

        v(INFO_SEM);
    }
}
