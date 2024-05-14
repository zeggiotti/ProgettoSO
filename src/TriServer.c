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
#include <errno.h>
#include "data.h"

void printError(const char *);
void init_data();
void init_board();
int check_board();
void removeIPCs();
void signal_handler(int);
void set_sig_handlers();
int p(int, int);
void v(int, int);

// Id del seg. di memoria condivisa che contiene i dati della partita.
int lobbyDataId = 0;

// Indirizzo di memoria condivisa che contiene i dati della partita.
struct lobby_data *info = NULL;

// Indirizzo di memoria condivisa che contiene la matrice di gioco.
char *board = NULL;

// Timestamp dell'ultima pressione di Ctrl+C.
int sigint_timestamp = 0;

// Set di segnali ricevibili dal processo.
sigset_t processSet;

// Informazioni locali al server per gestire la partita.
struct matchinfo {
    int players_ready;
    int turn;
} matchinfo;

int main(int argc, char *argv[]){

    // il timeout deve essere un valore numerico.
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
        // stanno partecipando due giocatori. Se così non è si aspetta ancora. Permette di non fraintendere
        // i segnali SIGINT ecc...
        int gameIsReady = 0;
        do {
            pause();
            
            p(INFO_SEM, NOINT);
                
            if(info->client_pid[0] != 0 && info->client_pid[1] != 0)
                gameIsReady = 1;

            v(INFO_SEM, NOINT);
        } while(!gameIsReady);

        printf("%s\n", GAME_STARTING);

        init_board();

        p(INFO_SEM, NOINT);

        info->game_started = 1;

        matchinfo.players_ready = 0;
        matchinfo.turn = 0;

        int partitaInCorso = info->game_started;
        printf("Player 1: %d\nPlayer 2: %d\n", info->client_pid[0], info->client_pid[1]);

        // La partita è pronta. Lo si comunica ai client facendo riprendere la loro esecuzione, i quali visualizzano la matrice
        // a schermo e aspettano.
        if(kill(info->client_pid[0], SIGUSR1) == -1)
            printError(SIGUSR1_SEND_ERR);

        if(kill(info->client_pid[1], SIGUSR1) == -1)
            printError(SIGUSR1_SEND_ERR);

        v(INFO_SEM, NOINT);

        // Gestione della partita. Sono necessarie le P e le V perché non si può essere sicuri che sia un solo processo
        // ad accedere ad info in un istante, dal momento che si legge e si modifica info->game_started.
        
        int semaphore_turn = CLIENT1_SEM;

        while(partitaInCorso){
            v(semaphore_turn, WITHINT);

            // Questo non è un polling: il server attende sul semaforo in una attesa non attiva. Il ciclo serve per distinguere
            // i casi in cui si esce dalla P perché si ha ottenuto il via libera, da quelli in cui si esce perché è stato ricevuto un
            // segnale. In tal caso, p ritorna -1 quando errno == EINTR.
            while(p(SERVER, WITHINT) == -1);

            partitaInCorso = partitaInCorso && !check_board();
            p(INFO_SEM, NOINT);
            info->game_started = partitaInCorso;
            info->move_made = 0;
            v(INFO_SEM, NOINT);

            if(partitaInCorso){
                printf("Giocatore %d con pid %d ha giocato una mossa.\n", matchinfo.turn + 1, info->client_pid[matchinfo.turn]);
                matchinfo.turn = (matchinfo.turn == 0) ? 1 : 0;
                semaphore_turn = (semaphore_turn == CLIENT1_SEM) ? CLIENT2_SEM : CLIENT1_SEM;
            }
        }

        // Partita terminata. Che sia in parità o che qualcuno abbia vinto, si svegliano i client per far rimuovere i loro IPC,
        // in modo che possano accedere ai semafori prima che essi vengano rimossi.
        
        v(CLIENT1_SEM, WITHINT);
        p(SERVER, WITHINT);
        v(CLIENT2_SEM, WITHINT);
        p(SERVER, WITHINT);

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
 * @param: semnum - il semaforo su cui eseguire p
 * @param: no_int - dice se si abilita la cattura di segnali durante l'attesa su semaforo.
*/
int p(int semnum, int no_int){

    // Disabilita la cattura di tutti i segnali (catturabili)
    if(no_int){
        sigset_t noInterruptionSet, oldSet;

        sigfillset(&noInterruptionSet);
        sigprocmask(SIG_SETMASK, &noInterruptionSet, &oldSet);

        processSet = oldSet;
    }

    struct sembuf p;
    p.sem_num = semnum;
    p.sem_op = -1;
    p.sem_flg = 0;

    int code;
    if((code = semop(info->semaphores, &p, 1)) == -1){
        // Vero errore solo se non si riceve EINTR ( = si è ricevuto un segnale)
        if(errno != EINTR)
            printError(P_ERR);
    }

    return code;
}

/**
 * Procedura V Signal.
 * @param: semnum - il semaforo su cui eseguire p
 * @param: no_int - dice se si abilita la cattura di segnali durante l'attesa su semaforo.
*/
void v(int semnum, int no_int){
    struct sembuf v;
    v.sem_num = semnum;
    v.sem_op = 1;
    v.sem_flg = 0;

    if(semop(info->semaphores, &v, 1) == -1)
        printError(V_ERR);

    // Si abilitano i segnali per attese su semafori in cui si erano disabilitati.
    if(no_int)
        sigprocmask(SIG_SETMASK, &processSet, NULL);
}

/**
 * Inizializza i dati necessari a giocare, ovvero i dati riguardanti client, server e la generale gestione della partita (lobby).
*/
void init_data(char *argv[]){
    key_t lobbyShmKey = ftok(PATH_TO_FILE, FTOK_KEY);
    if(lobbyShmKey == -1)
        printError(FTOK_ERR);

    int sems = semget(IPC_PRIVATE, 5, S_IRUSR | S_IWUSR);
    if(sems == -1){
        printError(SEM_ERR);
    }

    short values[] = {1, 1, 0, 0, 0};
    union semun arg;
    arg.array = values;
    if(semctl(sems, 0, SETALL, arg) == -1){
        printError(SEM_ERR);
    }

    // Dopo aver inizializzato i semafori, si esegue una P su essi per inizializzare i dati condivisi.
    // Questo per permettere ai client di vedere la presenza di una partita, ma non accedervi ancora perché in fase
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
        /** NB: DA SEGMENTATION FAULT!*/
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
 * Inizializza la matrice a spazi vuoti
*/
void init_board(){
    for(int i = 0; i < 9; i++){
        board[i] = ' ';
    }
}

/**
 * Controlla se la partita è finita. In qualunque situazione terminale, ritorna 1,
 * altrimenti 0 se la partita può continuare.
*/
int check_board(){
    int isDraw = 1;
    for(int i = 0; i < 3; i++){
        for(int j = 0; j < 3; j++){
            if(board[(3 * i) + j] == ' ')
                isDraw = 0;
        }
    }

    if(!isDraw){

        int ended = 0;
        char winner_sign = ' ';

        for(int i = 0; i < 3; i++){
            if(board[(3 * i)] == board[(3 * i) + 1] && board[(3 * i) + 1] == board[(3 * i) + 2] && board[(3 * i)] != ' '){
                ended = 1;
                winner_sign = board[(3 * i)];
            }
        }

        for(int i = 0; i < 3; i++){
            if(board[i] == board[3 + i] && board[3 + i] == board[6 + i] && board[i] != ' '){
                ended = 1;
                winner_sign = board[i];
            }
        }

        if(board[0] == board[4] && board[4] == board[8] && board[0] != ' '){
            ended = 1;
            winner_sign = board[0];
        }

        if(board[2] == board[4] && board[4] == board[6] && board[2] != ' '){
            ended = 1;
            winner_sign = board[2];
        }

        if(ended){
            int winner_player;
            if(winner_sign == info->signs[0])
                winner_player = 0;
            else
                winner_player = 1;
            info->winner = info->client_pid[winner_player];
        }
        
        return ended;

    } else return isDraw;
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
 * Rimuove gli IPC creati.
*/
void removeIPCs(){
    // Rimozione e staccamento di/da shm di lobby e matrice di gioco e semafori.
    if(semctl(info->semaphores, 0, IPC_RMID, 0) == -1){
        printf("eror");
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

/**
 * TODO: Aggiungere che quando un client quitta l'altro diventa automaticamente il player 1.
*/
void signal_handler(int sig){
    if(sig == SIGINT || sig == SIGHUP) {

        // Ritorna indietro per scrivere sopra al carattere ^C
        printf("\r");
        
        int now = time(NULL);
        if(now - sigint_timestamp < MAX_SECONDS || sig == SIGHUP) {

            // Pressione di Ctrl+C. Si fanno terminare i client e poi il server termina.
            p(INFO_SEM, NOINT);

            if(info->client_pid[0] != 0)
                if(kill(info->client_pid[0], SIGTERM) == -1)
                        printError(SIGTERM_SEND_ERR);

            if(info->client_pid[1] != 0)
                if(kill(info->client_pid[1], SIGTERM) == -1)
                    printError(SIGTERM_SEND_ERR);

            v(INFO_SEM, NOINT);

            removeIPCs();
            exit(0);

        } else {
            sigint_timestamp = now;
            printf("Per terminare l'esecuzione, premere Ctrl+C un'altra volta entro %d secondi.\n", MAX_SECONDS);
        }

    } else if (sig == SIGUSR2){
        // Un client ha premuto Ctrl+C. Se la partita è iniziata, l'altro client vince a tavolino. Altrimenti non si
        // controlla nulla: siamo in fase di attesa giocatori, chiunque può entrare o uscire dalla lobby.
        p(INFO_SEM, NOINT);

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
            
            printf("Partita terminata per abbandono.\n");
            removeIPCs();
            exit(0);
        }

        v(INFO_SEM, NOINT);
    } else if (sig == SIGUSR1){
        // Un client comunica di voler procedere.
        p(INFO_SEM, NOINT);

        /** FAQ: Può essere rimosso ??? */
        if(info->game_started){
            if(matchinfo.players_ready < 2){
                matchinfo.players_ready++;
            }
        }

        v(INFO_SEM, NOINT);
    }
}
