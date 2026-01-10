#include "common.hpp"
#include <iostream>

using namespace std;

int main() {
    cout << "[PRZEWODNIK] Uruchamiam się..." << endl;

    // Generowanie klucza (taki sam jak w main)
    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) { perror("[PRZEWODNIK] Błąd ftok"); return 1; }

    // Pobranie ID istniejącej pamięci (bez IPC_CREAT)
    int shm_id = shmget(key, sizeof(JaskiniaStan), 0666);
    if (shm_id == -1) { 
        perror("[PRZEWODNIK] Błąd shmget"); 
        return 1; 
    }

    // Dołączenie pamięci do procesu
    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) { perror("[PRZEWODNIK] Błąd shmat"); return 1; }

    // Odczyt danych ze współdzielonej struktury
    cout << "[PRZEWODNIK] Podłączono do Jaskini! ID pamięci: " << shm_id << endl;
    cout << "[PRZEWODNIK] Obecny kierunek ruchu: " << stan->kierunek_ruchu_kladka << endl;
    cout << "[PRZEWODNIK] Osoby na kładce: " << stan->osoby_na_kladce << endl;

    // Odłączenie pamięci (bez usuwania)
    shmdt(stan);

    cout << "[PRZEWODNIK] Kończę pracę." << endl;
    return 0;
}