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
#include <termios.h>

void printError(const char *);
void init_data();
void print_board();
void print_move_feedback();
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

/** TODO: Computer arriva troppo velocemente. */

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

// Username del giocatore
char username[USERNAME_DIM];
char opponent[USERNAME_DIM];

int timeout_over = 0;

// Indica se il client ha giocato una mossa (serve a gestire il Ctrl+C durante la partita).
int move_played = 0;

pid_t child;

// Set di segnali ricevibili dal processo.
sigset_t processSet;

int main(int argc, char *argv[]){

    if(argc < 2){
        printf("%s", CLIENT_TERMINAL_CMD);
        exit(0);
    }

    child = -1;
    srand(time(NULL));

    /**
     * Si controllano i parametri. Se si gioca contro il computer, si genera il processo figlio (sarà lui il computer).
     * Per questo motivo, molte delle stampe a video saranno impedite al computer.
    */
    if(argc == 3){
        if(argv[2][0] == '*' && argv[2][1] == '\0'){
            child = fork();
            if(child == -1)
                printError(NO_CHILD_CREATED_ERR);
        } else {
            printf("%s", CLIENT_TERMINAL_CMD);
            exit(0);
        }
    } else if(argc > 3) {
        printf("%s", CLIENT_TERMINAL_CMD);
        exit(0);
    }

    // Si imposta il proprio username
    if(child != 0){
        int i;
        for(i = 0; argv[1][i] != '\0'; i++){
            username[i] = argv[1][i];
        }
        username[i] = '\0';
    } else {
        char str[] = "Computer";
        int i;
        for(i = 0; str[i] != '\0'; i++){
            username[i] = str[i];
        }
        username[i] = '\0';
    }

    set_sig_handlers();

    tcgetattr(STDIN_FILENO, &termios);

    init_data();

    if(child != 0)
        printf("%s", CLEAR);

    // Comunica al server che ci si è collegati alla partita.
    // Lo si fa con P e V per permettere al server di gestire l'arrivo di un client uno alla volta,
    // sennò utente e computer potrebbero essere troppo veloci. Il programma funziona comunque, ma il server non stampa
    // l'arrivo di entrambi.
    p(INFO_SEM, NOINT);
    if(kill(server, SIGUSR1) == -1){
        v(INFO_SEM, NOINT);
        printError(SIGUSR1_SEND_ERR);
    }

    v(INFO_SEM, NOINT);

    if(child != 0)
        printf("%s\n", WAITING);

    // Si aspetta di essere in due!
    // Necessario il while per gestire il doppio Ctrl+C.
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

    int i;
    for(i = 0; info->usernames[!player][i] != '\0'; i++){
        opponent[i] = info->usernames[!player][i];
    }
    opponent[i] = '\0';

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
                do {
                    move();
                    print_board();
                } while(!move_played);
            } else {
                pc_move();
            }

            move_played = 0;

            if(child != 0){
                print_move_feedback();
                remove_terminal_echo();
            }

            v(SERVER, WITHINT);
        } else {
            // La partita è terminata
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
    struct sigaction act;
    act.sa_flags = ~SA_RESTART;
    act.sa_handler = signal_handler;
    sigaction(SIGINT, &act, NULL);

    if(signal(SIGTERM, signal_handler) == SIG_ERR)
        printError(SIGTERM_HANDLER_ERR);

    if(signal(SIGUSR1, signal_handler) == SIG_ERR)
        printError(SIGUSR1_HANDLER_ERR);

    if(signal(SIGHUP, signal_handler) == SIG_ERR)
        printError(SIGHUP_HANDLER_ERR);
    
}

/**
 * Stampa un messaggio d'errore. Prima di terminare, rimuove tutti gli IPC creati.
 * Se a stampare è il computer, aggiunge [PC] ad inizio messaggio.
*/
void printError(const char *msg){
    char buff[256];

    if(child == 0){
        snprintf(buff, 255, "[PC] %s", msg);
    } else {
        snprintf(buff, 255, "%s", msg);
    }

    printf("%s\n", buff);
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
    if((code = semop(semaphores, &p, 1)) == -1){
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

    if(semop(semaphores, &v, 1) == -1){
        printf("%d\n", errno);
        printError(V_ERR);
    }

    // Si abilitano i segnali per attese su semafori in cui si erano disabilitati.
    if(no_int)
        sigprocmask(SIG_SETMASK, &processSet, NULL);
}

/**
 * Stampa la matrice di gioco.
*/
void print_board(){
    printf("%s", CLEAR);

    printf("%s vs %s\n\n", username, opponent);

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
 * Stampa a video un feedback sul turno passato.
*/
void print_move_feedback(){
    if(info->move_made[0] == 'N' && info->move_made[1] == 'V')
        printf("> Hai giocato una mossa non valida.\n");
    else if(info->move_made[0] == 'T' && info->move_made[1] == 'O')
        printf("> Non hai giocato una mossa entro lo scadere dei secondi.\n");
    else
        printf("> Hai giocato la mossa %s.\n", info->move_made);
}

/**
 * Esegue una mossa. Si suppone che ad inserimento errato o scandere del timeout equivalga concedere il turno.
*/
void move(){
    char coord[4] = {0};
    char output[51] = {0};

    // Numero di secondi disponibili
    int seconds = info->timeout;
    int bytesRead = -1;

    if(seconds > 0)
        printf("\nTempo a disposizione: %d secondi.\n", seconds);
    else
        printf("\nTempo a disposizione illimitato.\n");

    printf("\r%s\r", BLANK_LINE);
    
    snprintf(output, 50, "> Inserisci una coordinata %c: ", info->signs[player]);
    write(STDOUT_FILENO, output, 50);

    timeout_over = 0;

    // Se seconds == 0 non bisogna impostare un alarm. Anche alarm(0) va bene però cosi sembra più liscio.
    if(seconds > 0){
        /** NB:*/
        // Fa in modo che l'alarm faccia chiudere la read(). Se in delta funziona senza meglio, sennò AMEN.
        struct sigaction act;
        act.sa_flags = ~SA_RESTART;
        act.sa_handler = signal_handler;
        sigaction(SIGALRM, &act, NULL);
        alarm(seconds);
    }

    bytesRead = read(STDIN_FILENO, coord, 4);
    
    if(bytesRead <= 0 && !timeout_over)
        return;
    else {
        move_played = 1;
    }
    
    move_played = 1;

    if(timeout_over){
        info->move_made[0] = 'T';
        info->move_made[1] = 'O';
        info->move_made[2] = '\0';
        return;
    }

    // Controllo sulla coordinata in input
    if(bytesRead <= 0 || coord[2] != '\n' || !(coord[1] >= '1' && coord[1] <= '3') || 
        !((coord[0] >= 'a' && coord[0] <= 'c') || (coord[0] >= 'A' && coord[0] <= 'C'))){
            info->move_made[0] = 'N';
            info->move_made[1] = 'V';
            info->move_made[2] = '\0';
            return;
    }

    coord[2] = '\0';

    // Si calcola la coordinata inserita
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

        // Aggiunta del proprio username
        int i;
        for(i = 0; username[i] != '\0'; i++){
            info->usernames[info->num_clients][i] = username[i];
        }
        info->usernames[info->num_clients][i] = '\0';
        
        info->num_clients++;
    }

    if(semop(semaphores, &v, 1) == -1)
        printError(V_ERR);

    sigprocmask(SIG_SETMASK, &processSet, NULL);
}

/**
 * Rimuove il client dalla partita, ovvero lo toglie dall'array di client collegati e dagli username.
*/
void remove_pid_from_game(){
    p(INFO_SEM, NOINT);

    int index;
    if(info->client_pid[0] == getpid())
        index = 0;
    else if(info->client_pid[1] == getpid())
        index = 1;

    info->client_pid[index] = 0;

    // Si toglie anche il suo username
    for(int i = 0; info->usernames[index][i] != '\0'; i++){
        info->usernames[index][i] = '\0';
    }

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

        // Reimposta l'handler per SIGINT
        if(sig == SIGINT){
            struct sigaction act;
            act.sa_flags = ~SA_RESTART;
            act.sa_handler = signal_handler;
            sigaction(SIGINT, &act, NULL);
        }

        // Bisogna usare write perché printf bufferizza e viene stampato comunque ^C
        if(child != 0)
            write(STDOUT_FILENO, "\b\b  \b\b", 7);

        int now = time(NULL);
        if(now - sigint_timestamp < MAX_SECONDS || sig == SIGHUP) {

            if(child != 0){
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
            }

        } else {
            sigint_timestamp = now;
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
    } else if(sig == SIGALRM) {
        timeout_over = 1;
    }
}
