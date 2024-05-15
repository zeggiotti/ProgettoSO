#ifndef DATA_H
#define DATA_H
#include <sys/types.h>
#include <sys/sem.h>

#define INFO_SEM 0      // Semaforo che gestisce l'accesso alle informazioni della partita.
#define CLIENT1_SEM 1   // Semaforo per sincronizzare il client 1.
#define CLIENT2_SEM 2   // Semaforo per sincronizzare il client 2.
#define SERVER 3        // Semaforo per sincronizzare il server.

#define WITHINT 0
#define NOINT 1

#define MAX_SECONDS 1   // Numero massimo di secondi che devono passare tra un Ctrl+C e l'altro.

#define USERNAME_DIM 64

#define CLEAR "\033[H\033[J"
#define BLANK_LINE "                                               "
#define BOARD_TAB "   "
#define FIELD_TAB " "

#define HELP_MSG "\nHELP - per eseguire il server correttamente:\n\n    ./TriServer timeout c1 c2\n\ndove:\n-timeout: il tempo a disposizione per ogni mossa\n-c1: il carattere del giocatore 1\n-c2: il carattere del giocatore 2\n\n"
#define CLIENT_TERMINAL_CMD "\nPuoi eseguire il client in due modalità:\n\n    ./TriClient nomeUtente (per giocare contro un altro utente)\n    ./TriClient nomeUtente \\* (per giocare contro il Computer)\n\n"

#define PATH_TO_FILE "data/keyfile.txt"
#define FTOK_KEY 'f'

#define FTOK_ERR "Errore nella generazione di una chiave con ftok()."

#define SIGINT_HANDLER_ERR "Errore in impostazione del SIGINT handler..."
#define SIGUSR1_HANDLER_ERR "Errore in impostazione del SIGUSR1 handler..."
#define SIGUSR2_HANDLER_ERR "Errore in impostazione del SIGUSR2 handler..."
#define SIGTERM_HANDLER_ERR "Errore in impostazione del SIGTERM handler..."
#define SIGHUP_HANDLER_ERR "Errore in impostazione del SIGHUP handler..."
#define SIGALRM_HANDLER_ERR "Errore in impostazione del SIGALRM handler..."

#define SIGCONT_SEND_ERR "Errore in invio di SIGCONT al giocatore."
#define SIGTERM_SEND_ERR "Errore in invio di SIGTERM al giocatore."
#define SIGUSR1_SEND_ERR "Errore in invio di SIGUSR1 al server."
#define SIGUSR2_SEND_ERR "Errore in invio di SIGUSR2 al server."

#define P_ERR "Errore in esecuzione di P"
#define V_ERR "Errore in esecuzione di V"

#define NO_CHILD_CREATED_ERR "Errore in creazione del processo Computer."

#define SHMAT_ERR "Errore di collegamento al segmento di memoria condivisa."
#define BOARD_SHM_ERR "Errore di creazione della matrice di gioco (memoria condivisa)."
#define SHMDT_ERR "Errore in scollegamento da memoria condivisa."
#define SHM_DEL_ERR "Errore in rimozione della memoria condivisa."
#define SEM_ERR "Errore in creazione o inizializzazione del set di semafori."
#define SEM_DEL_ERR "Errore in rimozione del set di semafori."

#define WAITING_FOR_PLAYERS "> In attesa di giocatori..."

#define NO_GAME_FOUND "Non è stata trovata alcuna partita a cui partecipare.\nEsegui un server per iniziare a giocare."
#define GAME_EXISTING_ERR "Una partita è già iniziata. Riprova più tardi."
#define GAME_STARTING "> La partita è iniziata."
#define WAITING "> In attesa di un giocatore..."
#define QUITTING "> Abbandono..."

#define SERVER_STOPPED_GAME "> Partita terminata dal server."
#define RESIGNED_GAME "> Partita terminata per abbandono."

#define GAME_ENDED "> Partita terminata."
#define GAME_WON "> L'avversario ha abbandonato. Hai vinto a tavolino!"
#define YOU_WON "Hai vinto!"
#define YOU_LOST "Hai perso."
#define DRAW "Si è conclusa in parità."

struct lobby_data {
    pid_t server_pid;
    pid_t client_pid[2];
    char usernames[2][USERNAME_DIM];
    int num_clients;
    int timeout;
    char signs[2];          // Caratteri che useranno i client.
    int board_shmid;        // Id di seg. di mem. condivisa con la matrice di gioco.
    int semaphores;         // Id del set di semafori.
    int game_started;       // (Booleano) indica se la partita è iniziata o meno.
    pid_t winner;
    char move_made[3];          // Indica la mossa giocata sulla matrice.
};

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

#endif