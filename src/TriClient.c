#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>
#include <signal.h>
#include "data.h"
#include <errno.h>
#include <string.h>

void printError(const char *);
void init_data();
void signal_handler(int);
void removeIPCs();
void remove_pid_from_game();
void set_sig_handlers();
void p(int);
void v(int);

// Id del seg. di memoria condivisa che contiene i dati della partita.
int lobbyDataId = 0;

// Indirizzo di memoria condivisa che contiene i dati della partita.
struct lobby_data *info;

// Pid del server, salvato per non dover accedere alle info con p e v.
pid_t server;

// Indirizzo di memoria condivisa che contiene la matrice di gioco.
char *board = NULL;

int main(int argc, char *argv[]){

    set_sig_handlers();

    init_data();

    // Comunica al server che ci si è collegati alla partita.
    if(kill(server, SIGUSR1) == -1)
        printError(SIGUSR1_SEND_ERR);

    printf("%s\n", WAITING);

    // Si attende di essere in due giocatori.
    pause();

    printf("%s\n", GAME_STARTING);

    pause();

    removeIPCs();

}

/**
 * Imposta gli handler dei segnali da catturare.
*/
void set_sig_handlers(){
    if(signal(SIGINT, signal_handler) == SIG_ERR)
        printError(SIGINT_HANDLER_ERR);

    if(signal(SIGTERM, signal_handler) == SIG_ERR)
        printError(SIGTERM_HANDLER_ERR);

    if(signal(SIGUSR1, signal_handler) == SIG_ERR)
        printError(SIGUSR1_HANDLER_ERR);

    if(signal(SIGHUP, signal_handler) == SIG_ERR)
        printError(SIGHUP_HANDLER_ERR);
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
 * Ottiene i dati inizializzati dal server riguardo la partita da giocare.
*/
void init_data(){
    key_t lobbyShmKey = ftok(PATH_TO_FILE, FTOK_KEY);
    if(lobbyShmKey == -1)
        printError(FTOK_ERR);

    lobbyDataId = shmget(lobbyShmKey, sizeof(struct lobby_data), S_IRUSR | S_IWUSR);
    if(lobbyDataId == -1)
        printError(NO_GAME_FOUND);

    info = shmat(lobbyDataId, NULL, 0);
    if(info == (void *) -1)
        printError(SHMAT_ERR);

    // Si accede ai dati con p e v per evitare conflitti di r/w sugli stessi dati.
    p(INFO_SEM);

    server = info->server_pid;

    board = shmat(info->board_shmid, NULL, 0);
    if(board == (void *) -1){
        v(INFO_SEM);
        printError(SHMAT_ERR);
    }

    if(info->num_clients > 1){
        v(INFO_SEM);
        printError(GAME_EXISTING_ERR);
    } else {
        info->client_pid[info->num_clients] = getpid();
        info->num_clients++;
    }

    v(INFO_SEM);
}

/**
 * Rimuove il client dalla partita, ovvero lo toglie dall'array di client collegati.
*/
void remove_pid_from_game(){
    p(INFO_SEM);

    if(info->client_pid[0] == getpid())
        info->client_pid[0] = 0;
    else if(info->client_pid[1] == getpid())
        info->client_pid[1] = 0;

    info->num_clients--;

    v(INFO_SEM);
}

/**
 * Rimuove i segmenti di memoria a cui è collegato. A rimuovere i semafori penserà il server.
*/
void removeIPCs(){
    if(info != NULL){
        if(shmdt(info) == -1){
            printf(SHMDT_ERR);
        }
    }

    if(board != NULL){
        if(shmdt(board) == -1){
            printf(SHMDT_ERR);
        }
    }
}

void signal_handler(int sig){
    if(sig == SIGINT || sig == SIGHUP){
        // Si notifica al server che si vuole abbandonare la partita dopo aver rimosso il client
        // dalle info di gioco e rimosso gli IPC.
        remove_pid_from_game();
        removeIPCs();

        if(kill(server, SIGUSR2) == -1)
            printError(SIGUSR2_SEND_ERR);

        exit(0);
    } else if(sig == SIGTERM){
        // Terminazione causata dal server.
        removeIPCs();
        printf(SERVER_STOPPED_GAME);
        exit(0);
    }
}
