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
    cout << "[PRZEWODNIK] Podłączono do Jaskini! ID: " << shm_id << endl;
    cout << "------------------------------------------" << endl;
    cout << "STAN BILETÓW (od Kasjera):" << endl;
    cout << " -> Trasa 1: " << stan->bilety_sprzedane_t1 << " / " << N1 << endl;
    cout << " -> Trasa 2: " << stan->bilety_sprzedane_t2 << " / " << N2 << endl;
    cout << "------------------------------------------" << endl;
    cout << "STAN KŁADKI:" << endl;
    cout << " -> Kierunek: " << stan->kierunek_ruchu_kladka << endl;
    cout << " -> Osoby:    " << stan->osoby_na_kladce << endl;

    // Odłączenie pamięci (bez usuwania)
    shmdt(stan);

    cout << "[PRZEWODNIK] Kończę pracę." << endl;
    return 0;
}