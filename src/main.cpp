#include "common.hpp"
#include <iostream>
#include <cstring>

using namespace std;

int shm_id = -1;
int sem_id = -1;

// Funkcja sprzątająca po Ctrl+C
void cleanup(int signum) {
    cout << "\n[MAIN] Zamykanie symulacji..." << endl;

    if (shm_id != -1) {
        shmctl(shm_id, IPC_RMID, NULL);
        cout << "[MAIN] Pamięć dzielona usunięta." << endl;
    }
    if (sem_id != -1) {
        semctl(sem_id, 0, IPC_RMID);
        cout << "[MAIN] Semafory usunięte." << endl;
    }
    exit(0);
}

int main() {
    signal(SIGINT, cleanup);

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) { perror("Błąd ftok"); return 1; }

    // Tworzenie pamięci dzielonej
    shm_id = shmget(key, sizeof(JaskiniaStan), IPC_CREAT | 0666);
    if (shm_id == -1) { perror("Błąd shmget"); return 1; }

    // Inicjalizacja stanu (zerowanie)
    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) { perror("Błąd shmat"); cleanup(0); }

    memset(stan, 0, sizeof(JaskiniaStan));
    stan->kierunek_ruchu_kladka = DIR_NONE;
    shmdt(stan);

    cout << "[MAIN] Pamięć gotowa. ID: " << shm_id << endl;

    // Tworzenie semaforów 
    key_t sem_key = ftok(FTOK_FILE, SEM_ID);
    sem_id = semget(sem_key, 1, IPC_CREAT | 0666);
    if (sem_id == -1) { perror("Błąd semget"); cleanup(0); }

    cout << "[MAIN] Semafory gotowe. ID: " << sem_id << endl;
    cout << "[MAIN] Symulacja działa. Ctrl+C by zakończyć." << endl;

    while (true) sleep(1);

    return 0;
}