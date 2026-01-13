#include "common.hpp"
#include <iostream>

using namespace std;

int main() {
    cout << "[KASJER] Otwieram kasę..." << endl;

    // Pobranie klucza
    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) { perror("[KASJER] Błąd ftok"); return 1; }

    // Pobranie ID pamięci
    int shm_id = shmget(key, sizeof(JaskiniaStan), 0666);
    if (shm_id == -1) { 
        perror("[KASJER] Błąd shmget (czy symulacja jest uruchomiona?)"); 
        return 1; 
    }

    // Dołączenie pamięci
    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) { perror("[KASJER] Błąd shmat"); return 1; }

    cout << "[KASJER] Podłączono. Zaczynam sprzedaż biletów." << endl;

    // Pętla pracy Kasjera
    while (true) {
        // Sprawdzenie miejsca na Trasie 1
        if (stan->bilety_sprzedane_t1 < N1) {
            stan->bilety_sprzedane_t1++;
            cout << "[KASJER] Sprzedano bilet na Trasę 1. (Razem: " 
                 << stan->bilety_sprzedane_t1 << "/" << N1 << ")" << endl;
        } else {
            cout << "[KASJER] Trasa 1 pełna! Czekam na nową grupę..." << endl;
        }

        // Sprawdzenie miejsca na Trasie 2
        if (stan->bilety_sprzedane_t2 < N2) {
            stan->bilety_sprzedane_t2++;
            cout << "[KASJER] Sprzedano bilet na Trasę 2. (Razem: " 
                 << stan->bilety_sprzedane_t2 << "/" << N2 << ")" << endl;
        }

        sleep(2);
    }

    shmdt(stan);
    return 0;
}