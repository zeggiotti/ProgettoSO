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
void remove_terminal_echo();
void restore_terminal_echo();
void p(int, int);
void v(int, int);

// Attributi del terminale
struct termios termios;

// Id del seg. di memoria condivisa che contiene i dati della partita.
int lobbyDataId = 0;

// Indirizzo di memoria condivisa che contiene i dati della partita.
struct lobby_data *info;

// Pid del server, salvato per non dover accedere alle info con p e v.
pid_t server;

// L'id di semafori usati per sincronizzare i processi. Se ne tiene una copia per eseguire v(SERVER) anche dopo aver scollegato
// la memoria condivisa, e dunque anche info->semaphores.
int semaphores;

// Indirizzo di memoria condivisa che contiene la matrice di gioco.
char *board = NULL;

int player;

// Set di segnali ricevibili dal processo.
sigset_t processSet;

int main(int argc, char *argv[]){

    set_sig_handlers();

    tcgetattr(STDIN_FILENO, &termios);

    init_data();

    // Comunica al server che ci si è collegati alla partita.
    if(kill(server, SIGUSR1) == -1)
        printError(SIGUSR1_SEND_ERR);

    printf("%s\n", WAITING);

    // Si aspetta di essere in due!
    pause();

    printf("%s\n", GAME_STARTING);

    p(INFO_SEM, NOINT);

    // Si va a scoprire il numero di giocatore
    if(info->client_pid[0] == getpid())
        player = 0;
    else
        player = 1;

    // Indica se la partita è in corso o se è terminata (parità o vittoria)
    /** FAQ: Si può assegnare il valore di info->game_started ??? */
    int partitaInCorso = 1;

    v(INFO_SEM, NOINT);

    // Stampa la matrice vuota
    print_board();
    
    remove_terminal_echo();

    int my_semaphore = (player == 0) ? CLIENT1_SEM : CLIENT2_SEM;

    while(partitaInCorso) {
        p(my_semaphore, WITHINT);

        // Svuota il buffer del terminale e ignora tutti i caratteri inseriti.
        tcflush(STDIN_FILENO, TCIFLUSH);
        restore_terminal_echo();

        // In ogni caso si stampa lo stato della partita
        print_board();

        p(INFO_SEM, NOINT);
        // Il server comunica se la partita è terminata o meno
        partitaInCorso = info->game_started;
        v(INFO_SEM, NOINT);

        if(partitaInCorso){
            // La partita non è finita. Si procede.
            move();
            p(INFO_SEM, NOINT);
            info->move_made = 1;
            v(INFO_SEM, NOINT);
            print_board();

            remove_terminal_echo();

            v(SERVER, WITHINT);
        } else {
            printf("Partita terminata.\n");
        }
    }

    // Nel normale flusso d'esecuzione: i client si rimuovono dalla partita terminata e fanno procedere il server (che sta aspettando)
    // alla rimozione degli IPCs
    remove_pid_from_game();
    removeIPCs();

    v(SERVER, WITHINT);

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
 * Disabilita la visualizzazione dei caratteri inseriti a linea di comando.
*/
void remove_terminal_echo(){
    termios.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios);
}

/**
 * Abilita la visualizzazione dei caratteri inseriti a linea di comando.
*/
void restore_terminal_echo(){
    termios.c_lflag |= ECHO;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios);
}

/**
 * Procedura P Wait.
 * @param: semnum - il semaforo su cui eseguire p
 * @param: no_int - dice se si abilita la cattura di segnali durante l'attesa su semaforo.
*/
void p(int semnum, int no_int){

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

    /**
     * A differenza del server, non ci importa controllare di ricevere EINTR perché un client termina al primo segnale
     * SIGINT/SIGTERM/SIGHUP. Dunque alla loro ricezione, si è sicuri di terminare l'esecuzione, dunque non ci importa di controllare
     * il flusso d'esecuzione successivo alla p.
    */

    if(semop(semaphores, &p, 1) == -1)
        printError(P_ERR);
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

    if(semop(semaphores, &v, 1) == -1)
        printError(V_ERR);

    // Si abilitano i segnali per attese su semafori in cui si erano disabilitati.
    if(no_int)
        sigprocmask(SIG_SETMASK, &processSet, NULL);
}

/**
 * Stampa la matrice di gioco.
*/
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

/**
 * Esegue una mossa. Si suppone che ad inserimento errato equivalga concedere il turno ???.
 * TODO: Controllo sui dati in input.
*/
void move(){
    int riga, colonna;
    printf("Riga: ");
    scanf("%d", &riga);
    printf("Colonna: ");
    scanf("%d", &colonna);

    p(INFO_SEM, NOINT);
    if(board[(riga * 3) + colonna] == ' ')
        board[(riga * 3) + colonna] = info->signs[player];
    v(INFO_SEM, NOINT);
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

    struct sembuf v;
    v.sem_num = 0;
    v.sem_op = 1;
    v.sem_flg = 0;

    if(semop(info->semaphores, &p, 1) == -1)
        printError(P_ERR);

    server = info->server_pid;
    semaphores = info->semaphores;

    board = shmat(info->board_shmid, NULL, 0);
    if(board == (void *) -1){
        if(semop(info->semaphores, &v, 1) == -1)
            printError(V_ERR);
        printError(SHMAT_ERR);
    }

    if(info->num_clients > 1){
        if(semop(info->semaphores, &v, 1) == -1)
            printError(V_ERR);
        printError(GAME_EXISTING_ERR);
    } else {
        info->client_pid[info->num_clients] = getpid();
        info->num_clients++;
    }

    if(semop(semaphores, &v, 1) == -1)
        printError(V_ERR);

    sigprocmask(SIG_SETMASK, &processSet, NULL);
}

/**
 * Rimuove il client dalla partita, ovvero lo toglie dall'array di client collegati.
*/
void remove_pid_from_game(){
    p(INFO_SEM, NOINT);

    if(info->client_pid[0] == getpid())
        info->client_pid[0] = 0;
    else if(info->client_pid[1] == getpid())
        info->client_pid[1] = 0;

    info->num_clients--;

    v(INFO_SEM, NOINT);
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

        restore_terminal_echo();

        exit(0);

    } else if(sig == SIGTERM){
        // Terminazione causata dal server.
        removeIPCs();

        restore_terminal_echo();

        printf("\r%s\n", SERVER_STOPPED_GAME);
        exit(0);
    }
}
