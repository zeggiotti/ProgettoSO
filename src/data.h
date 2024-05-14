#ifndef DATA_H
#define DATA_H
#include <sys/types.h>
#include <sys/sem.h>

#define CLEAR "\033[H\033[J"

#define INFO_SEM 0      // Semaforo che gestisce l'accesso alle informazioni della partita.
#define BOARD_SEM 1     // Semaforo che gestisce l'accesso alla matrice di gioco.
#define CLIENT1_SEM 2   // Semaforo per sincronizzare il client 1.
#define CLIENT2_SEM 3   // Semaforo per sincronizzare il client 2.
#define SERVER 4        // Semaforo per sincronizzare il server.

#define WITHINT 0
#define NOINT 1

#define MAX_SECONDS 1   // Numero massimo di secondi che devono passare tra un Ctrl+C e l'altro.

#define HELP_MSG "\nHELP - per eseguire il server correttamente:\n\n    ./TriServer timeout c1 c2\n\ndove:\n-timeout: il tempo a disposizione per ogni mossa\n-c1: il carattere del giocatore 1\n-c2: il carattere del giocatore 2\n\n"

#define PATH_TO_FILE "data/keyfile.txt"
#define FTOK_KEY 'f'

#define FTOK_ERR "Errore nella generazione di una chiave con ftok()."

#define SIGINT_HANDLER_ERR "Errore in impostazione del SIGINT handler..."
#define SIGUSR1_HANDLER_ERR "Errore in impostazione del SIGUSR1 handler..."
#define SIGUSR2_HANDLER_ERR "Errore in impostazione del SIGUSR2 handler..."
#define SIGTERM_HANDLER_ERR "Errore in impostazione del SIGTERM handler..."
#define SIGHUP_HANDLER_ERR "Errore in impostazione del SIGHUP handler..."

#define SIGCONT_SEND_ERR "Errore in invio di SIGCONT al giocatore."
#define SIGTERM_SEND_ERR "Errore in invio di SIGTERM al giocatore."
#define SIGUSR1_SEND_ERR "Errore in invio di SIGUSR1 al server."
#define SIGUSR2_SEND_ERR "Errore in invio di SIGUSR2 al server."

#define P_ERR "Errore in esecuzione di P"
#define V_ERR "Errore in esecuzione di V"

#define SHMAT_ERR "Errore di collegamento al segmento di memoria condivisa."
#define BOARD_SHM_ERR "Errore di creazione della matrice di gioco (memoria condivisa)."
#define SHMDT_ERR "Errore in scollegamento da memoria condivisa."
#define SHM_DEL_ERR "Errore in rimozione della memoria condivisa."
#define SEM_ERR "Errore in creazione o inizializzazione del set di semafori."
#define SEM_DEL_ERR "Errore in rimozione del set di semafori."

#define NO_GAME_FOUND "Non è stata trovata alcuna partita a cui partecipare. Esegui un server per iniziare a giocare."
#define GAME_EXISTING_ERR "Una partita è già iniziata. Riprova più tardi."
#define GAME_STARTING "La partita sta iniziando."
#define WAITING "In attesa di un giocatore..."

#define SERVER_STOPPED_GAME "Partita terminata dal server."

struct lobby_data {
    pid_t server_pid;
    pid_t client_pid[2];
    int num_clients;
    int timeout;
    char signs[2];          // Caratteri che useranno i client.
    int board_shmid;        // Id di seg. di mem. condivisa con la matrice di gioco.
    int semaphores;         // Id del set di semafori.
    int game_started;       // (Booleano) indica se la partita è iniziata o meno.
    pid_t winner;
    int move_made;          // Impostato a 1 dal client quando esegue una mossa.
};

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

#endif