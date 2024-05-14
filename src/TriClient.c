#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "data.h"
#include <errno.h>
#include <string.h>
#include <termios.h>

void printError(const char *);
void init_data();
void print_board();
void move();
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

int semaphores;

// Indirizzo di memoria condivisa che contiene la matrice di gioco.
char *board = NULL;

int player;

// Set di segnali ricevibili dal processo.
sigset_t processSet;

int main(int argc, char *argv[]){

    set_sig_handlers();

    init_data();

    // Comunica al server che ci si è collegati alla partita.
    if(kill(server, SIGUSR1) == -1)
        printError(SIGUSR1_SEND_ERR);

    printf("%s\n", WAITING);

    pause();

    printf("%s\n", GAME_STARTING);

    p(INFO_SEM);

    // Si va a scoprire il numero di giocatore
    if(info->client_pid[0] == getpid())
        player = 0;
    else
        player = 1;

    v(INFO_SEM);

    // Stampa la matrice vuota
    print_board();
    
    // RIMUOVERE LA ROBA DI TERMIOS SE NON VA IN LAB
    struct termios termios;
    tcgetattr(STDIN_FILENO, &termios);
    termios.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios);

    // Indica se la partita è in corso o se è terminata (parità o vittoria)
    int partitaInCorso = 1;

    int my_semaphore = player + CLIENT1_SEM;

    while(partitaInCorso) {
        p(my_semaphore);

        tcflush(STDIN_FILENO, TCIFLUSH);
        termios.c_lflag |= ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios);

        // In ogni caso si stampa lo stato della partita
        print_board();

        p(INFO_SEM);
        // Il server comunica se la partita è terminata o meno
        partitaInCorso = info->game_started;
        v(INFO_SEM);

        if(partitaInCorso){
            move();

            p(INFO_SEM);
            info->move_made = 1;
            v(INFO_SEM);

            print_board();

            termios.c_lflag &= ~ECHO;
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios);

            v(SERVER);

            /* Si sveglia il server (che è blocccato su pause()) per far procedere la partita
            if(kill(server, SIGUSR1) == -1)
                printError(SIGUSR1_SEND_ERR);*/
        } else {
            printf("Partita terminata.\n");
        }
    }

    /*
    do {
        v(INFO_SEM);
        // Si aspetta di essere svegliati dal server
        pause();

        tcflush(STDIN_FILENO, TCIFLUSH);
        termios.c_lflag |= ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios);

        // In ogni caso si stampa lo stato della partita
        print_board();

        p(INFO_SEM);
        // Il server comunica se la partita è terminata o meno
        partitaInCorso = info->game_started;
        v(INFO_SEM);

        if(partitaInCorso){
            move();

            p(INFO_SEM);
            info->move_made = 1;
            v(INFO_SEM);

            print_board();

            termios.c_lflag &= ~ECHO;
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios);

             Si sveglia il server (che è blocccato su pause()) per far procedere la partita
            if(kill(server, SIGUSR1) == -1)
                printError(SIGUSR1_SEND_ERR);
        } else {
            printf("Partita terminata.\n");
        }
            
    } while (partitaInCorso);
    */

    // Nel normale flusso d'esecuzione: i client si rimuovono dalla partita terminata e fanno procedere il server alla rimozione
    // degli IPCs
    remove_pid_from_game();
    removeIPCs();

    /*
    if(kill(server, SIGUSR1) == -1)
        printError(SIGUSR1_SEND_ERR);
    */

    v(SERVER);

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
    sigset_t noInterruptionSet, oldSet;

    sigfillset(&noInterruptionSet);
    sigprocmask(SIG_SETMASK, &noInterruptionSet, &oldSet);

    processSet = oldSet;
    
    struct sembuf p;
    p.sem_num = semnum;
    p.sem_op = -1;
    p.sem_flg = 0;

    if(semop(semaphores, &p, 1) == -1)
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

    if(semop(semaphores, &v, 1) == -1)
        printError(V_ERR);

    sigprocmask(SIG_SETMASK, &processSet, NULL);
}

void print_board(){
    printf("%s", CLEAR);

    for(int i = 0; i < 3; i++){
        for(int j = 0; j < 3; j++){
            printf("%c", board[(3 * i) + j]);
            if(j < 2)
                printf("|");
            else
                printf("\n");
        }
        if(i < 2)
            printf("_____\n");
    }
}

void move(){
    int riga, colonna;
    printf("Riga: ");
    scanf("%d", &riga);
    printf("Colonna: ");
    scanf("%d", &colonna);

    p(INFO_SEM);
    if(board[(riga * 3) + colonna] == ' ')
        board[(riga * 3) + colonna] = info->signs[player];
    v(INFO_SEM);
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
    sigset_t noInterruptionSet, oldSet;

    sigfillset(&noInterruptionSet);
    sigprocmask(SIG_SETMASK, &noInterruptionSet, &oldSet);

    processSet = oldSet;
    
    struct sembuf p;
    p.sem_num = 0;
    p.sem_op = -1;
    p.sem_flg = 0;

    if(semop(info->semaphores, &p, 1) == -1)
        printError(P_ERR);

    server = info->server_pid;
    semaphores = info->semaphores;

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

    struct sembuf v;
    v.sem_num = 0;
    v.sem_op = 1;
    v.sem_flg = 0;

    if(semop(semaphores, &v, 1) == -1)
        printError(V_ERR);

    sigprocmask(SIG_SETMASK, &processSet, NULL);
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

        // Ritorna indietro per scrivere sopra al carattere ^C
        printf("\r");
        
        // Si notifica al server che si vuole abbandonare la partita dopo aver rimosso il client
        // dalle info di gioco e rimosso gli IPC.
        remove_pid_from_game();
        removeIPCs();

        if(kill(server, SIGUSR2) == -1)
            printError(SIGUSR2_SEND_ERR);

        struct termios termios;
        tcgetattr(STDIN_FILENO, &termios);
        termios.c_lflag |= ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios);

        exit(0);

    } else if(sig == SIGTERM){
        // Terminazione causata dal server.
        removeIPCs();

        struct termios termios;
        tcgetattr(STDIN_FILENO, &termios);
        termios.c_lflag |= ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios);

        printf("\r%s\n", SERVER_STOPPED_GAME);
        exit(0);
    }
}
