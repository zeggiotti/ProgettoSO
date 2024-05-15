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
void pc_move();
void signal_handler(int);
void removeIPCs();
void remove_pid_from_game();
void set_sig_handlers();
void remove_terminal_echo();
void restore_terminal_echo();
int p(int, int);
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

// Istante dell'ultima pressione di Ctrl+C
int sigint_timestamp = 0;

// Indice nell'array info->client_pid del giocatore
int player;

// Indica se il client ha giocato una mossa (serve a gestire il Ctrl+C durante la partita).
int move_played = 0;

pid_t child;

// Set di segnali ricevibili dal processo.
sigset_t processSet;

int main(int argc, char *argv[]){

    child = -1;
    srand(time(NULL));

    /** TODO: Limita inserimento argomenti! 
     *        Metti i nomi a Giocatore e Computer.
    */
    if(argc > 1){
        if(argv[1][0] == '*' && argv[1][1] == '\0'){
            child = fork();
            if(child == -1)
                printError(NO_CHILD_CREATED_ERR);
        }
    }

    set_sig_handlers();

    if(child != 0)
        printf("%s", CLEAR);

    tcgetattr(STDIN_FILENO, &termios);

    init_data();

    // Comunica al server che ci si è collegati alla partita.
    if(kill(server, SIGUSR1) == -1)
        printError(SIGUSR1_SEND_ERR);

    if(child != 0)
        printf("%s\n", WAITING);

    // Si aspetta di essere in due!
    /** TODO: Controllo su pressione Ctrl+C in attesa. */
    int started;
    do {
        pause();

        p(INFO_SEM, NOINT);
        started = info->game_started;
        v(INFO_SEM, NOINT);
    } while (!started);

    if(child != 0)
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
    if(child != 0){
        print_board();
        remove_terminal_echo();
    }

    int my_semaphore = (player == 0) ? CLIENT1_SEM : CLIENT2_SEM;

    while(partitaInCorso) {
        while(p(my_semaphore, WITHINT) == -1);

        // Svuota il buffer del terminale e ignora tutti i caratteri inseriti.
        if(child != 0){
            tcflush(STDIN_FILENO, TCIFLUSH);
            restore_terminal_echo();
        }

        // In ogni caso si stampa lo stato della partita
        if(child != 0)
            print_board();

        p(INFO_SEM, NOINT);
        // Il server comunica se la partita è terminata o meno
        partitaInCorso = info->game_started;
        v(INFO_SEM, NOINT);

        if(partitaInCorso){
            // La partita non è finita. Si procede.
            if(child != 0){
                /** FAQ: Se non si mette il countdown questo while è necessario ??? */
                do {
                    move();
                } while(move_played == 0);
            } else {
                pc_move();
            }

            move_played = 0;

            if(child != 0){
                print_board();
                remove_terminal_echo();
            }

            v(SERVER, WITHINT);
        } else {
            if(child != 0){
                printf("\n%s", GAME_ENDED);
                if(info->winner == getpid())
                    printf(" %s\n\n", YOU_WON);
                else if(info->winner == info->server_pid)
                    printf(" %s\n\n", DRAW);
                else
                    printf(" %s\n\n", YOU_LOST);
            }
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
    if(child != 0)
        if(signal(SIGINT, signal_handler) == SIG_ERR)
            printError(SIGINT_HANDLER_ERR);

    if(signal(SIGTERM, signal_handler) == SIG_ERR)
        printError(SIGTERM_HANDLER_ERR);

    if(signal(SIGUSR1, signal_handler) == SIG_ERR)
        printError(SIGUSR1_HANDLER_ERR);

    if(signal(SIGHUP, signal_handler) == SIG_ERR)
        printError(SIGHUP_HANDLER_ERR);

    /*
    if(signal(SIGALRM, signal_handler) == SIG_ERR)
        printError(SIGALRM_HANDLER_ERR);
    */
    
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

    /**
     * A differenza del server, non ci importa controllare di ricevere EINTR perché un client termina al primo segnale
     * SIGINT/SIGTERM/SIGHUP. Dunque alla loro ricezione, si è sicuri di terminare l'esecuzione, dunque non ci importa di controllare
     * il flusso d'esecuzione successivo alla p.
    */

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
    printf("%s\n", CLEAR);
    printf(" %s%s1   2   3\n", FIELD_TAB, BOARD_TAB);
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
    printf("Per abbandonare, premere due volte Ctrl+C in %d secondi.\n", MAX_SECONDS);
}

/**
 * Esegue una mossa. Si suppone che ad inserimento errato equivalga concedere il turno ???.
 * TODO: Dopo Ctrl+C si vedono i malanni.
*/
void move(){
    char coord[4] = {0};
    char output[51] = {0};

    int seconds = info->timeout;
    int bytesRead = -1;

    printf("\nTempo a disposizione: %d secondi.\n", seconds);
    
    snprintf(output, 50, "\r> Inserisci una coordinata %c: ", info->signs[player]);
    write(STDOUT_FILENO, output, 50);

    /** NB:*/
    // Fa in modo che l'alarm faccia chiudere la read(). Se in delta funziona senza meglio, sennò AMEN.
    struct sigaction act;
    act.sa_flags = ~SA_RESTART;
    act.sa_handler = signal_handler;
    sigaction(SIGALRM, &act, NULL);
    alarm(seconds);

    bytesRead = read(STDIN_FILENO, coord, 4);

    move_played = 1;

    if(bytesRead <= 0){
        info->move_made[0] = 'N';
        info->move_made[1] = 'V';
        info->move_made[2] = '\0';
        return;
    }

    if(coord[2] != '\n' || !(coord[1] >= '1' && coord[1] <= '3') || 
        !((coord[0] >= 'a' && coord[0] <= 'c') || (coord[0] >= 'A' && coord[0] <= 'C'))){
            info->move_made[0] = 'N';
            info->move_made[1] = 'V';
            info->move_made[2] = '\0';
            return;
    }

    coord[2] = '\0';

    info->move_made[0] = coord[0];
    info->move_made[1] = coord[1];
    info->move_made[2] = '\0';

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
 * Esegue una mossa casuale generata dal Computer.
*/
void pc_move(){

    int riga, colonna;
    
    do {
        riga = rand() % 3;
        colonna = rand() % 3;
    } while (board[(riga * 3) + colonna] != ' ');

    board[(riga * 3) + colonna] = info->signs[player];

    info->move_made[0] = (char) ('a' + riga);
    info->move_made[1] = (char) ('1' + colonna);
    info->move_made[2] = '\0';

}

/**
 * Ottiene i dati inizializzati dal server riguardo la partita da giocare.
 * TODO: Metti bene i log a terminale del Computer.
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

        if(child != 0)
            printf("\r");

        int now = time(NULL);
        if(now - sigint_timestamp < MAX_SECONDS || sig == SIGHUP) {

            if(child != 0){
                printf("%s\n", BLANK_LINE);
                printf("%s\n\n", QUITTING);
            }
            
            // Si notifica al server che si vuole abbandonare la partita dopo aver rimosso il client
            // dalle info di gioco e rimosso gli IPC.
            remove_pid_from_game();
            removeIPCs();

            if(kill(server, SIGUSR2) == -1)
                printError(SIGUSR2_SEND_ERR);

            if(child != 0)
                restore_terminal_echo();

            exit(0);

        } else {
            sigint_timestamp = now;
            // Ritorna indietro per scrivere sopra al carattere ^C
            printf("  \033[A\n");
        }

    } else if(sig == SIGTERM){
        // Terminazione causata dal server.

        if(child != 0){
            printf("\r%s\n", BLANK_LINE);

            // P e V non necessarie: si è sicuri che info->winner ha già il valore che deve assumere.
            if(info->winner == info->server_pid)
                printf("%s\n\n", SERVER_STOPPED_GAME);
            else
                printf("%s\n\n", GAME_WON);
        }

        removeIPCs();

        if(child != 0)
            restore_terminal_echo();

        exit(0);
    } else if(sig == SIGALRM){
        
    }
}
