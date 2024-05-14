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

// Indice nell'array info->client_pid del giocatore
int player;

// Set di segnali ricevibili dal processo.
sigset_t processSet;

int main(int argc, char *argv[]){

    set_sig_handlers();

    printf("%s", CLEAR);

    tcgetattr(STDIN_FILENO, &termios);

    init_data();

    // Comunica al server che ci si è collegati alla partita.
    if(kill(server, SIGUSR1) == -1)
        printError(SIGUSR1_SEND_ERR);

    printf("%s\n", WAITING);

    // Si aspetta di essere in due!
    pause();

    printf("\n%s\n", GAME_STARTING);

    p(INFO_SEM, NOINT);

    // Si va a scoprire il numero di giocatore
    if(info->client_pid[0] == getpid())
        player = 0;
    else
        player = 1;

    // Indica se la partita è in corso o se è terminata (parità o vittoria)
    int partitaInCorso = info->game_started;

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
            print_board();

            remove_terminal_echo();

            v(SERVER, WITHINT);
        } else {
            printf("\n%s", GAME_ENDED);
            if(info->winner == getpid())
                printf(" %s\n\n", YOU_WON);
            else if(info->winner == info->server_pid)
                printf(" %s\n\n", DRAW);
            else
                printf(" %s\n\n", YOU_LOST);
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
 * NB: Potrebbe dare problemi in Delta.
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
    printf("%s%s\n", FIELD_TAB, CLEAR);
    printf(" %s1   2   3\n", BOARD_TAB);
    printf(" %s%s  .   .  \n", FIELD_TAB, BOARD_TAB);

    char righe[] = {'A', 'B', 'C'};

    for(int i = 0; i < 3; i++){
        printf("%s%c%s", FIELD_TAB, righe[i], BOARD_TAB);
        for(int j = 0; j < 3; j++){
            if(j > 0)
                printf(" ");

            printf("%c", board[(3 * i) + j]);

            if(j < 2)
                printf(" ");

            if(j < 2)
                printf("|");
            else
                printf("\n");
        }
        if(i < 2){
            printf("%s%s---+---+---", FIELD_TAB, BOARD_TAB);
            printf("\n");
        }
    }

    printf(" %s%s  '   '  \n\n", FIELD_TAB, BOARD_TAB);
}

/**
 * Esegue una mossa. Si suppone che ad inserimento errato equivalga concedere il turno ???.
 * TODO: Migliore acquisizione dati (interfaccia).
*/
void move(){
    char coord[4];
    printf("> Inserisci una coordinata %c: ", info->signs[player]);
    scanf("%3s", coord);

    if(coord[2] != '\0' || (coord[1] < '1' && coord[1] > '3') || 
        ((coord[0] < 'a' || coord[0] > 'c') && (coord[0] < 'A' || coord[0] > 'C'))){
        return;
    }

    int colonna = coord[1] - '1';
    int riga;
    if(coord[0] >= 'A' && coord[0] <= 'C')
        riga = coord[0] - 'A';
    else
        riga = coord[0] - 'a';

    
    if(board[(riga * 3) + colonna] == ' ')
        board[(riga * 3) + colonna] = info->signs[player];
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
        printf("%s\n", BLANK_LINE);
        printf("%s\n\n", QUITTING);
        
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
        printf("\r%s\n", BLANK_LINE);

        // P e V non necessarie: si è sicuri che info->winner ha già il valore che deve assumere.
        if(info->winner == info->server_pid)
            printf("%s\n\n", SERVER_STOPPED_GAME);
        else
            printf("%s\n\n", GAME_WON);

        removeIPCs();

        restore_terminal_echo();

        exit(0);
    }
}
