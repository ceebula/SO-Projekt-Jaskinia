#include "common.hpp"
#include <iostream>
#include <unistd.h>

using namespace std;

int main() {
    cout << "[PRZEWODNIK] Uruchamiam się..." << endl;

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) { perror("[PRZEWODNIK] Błąd ftok"); return 1; }

    int shm_id = shmget(key, sizeof(JaskiniaStan), 0600);
    if (shm_id == -1) { perror("[PRZEWODNIK] Błąd shmget"); return 1; }

    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) { perror("[PRZEWODNIK] Błąd shmat"); return 1; }

    cout << "[PRZEWODNIK] Podłączono. Jaskinia otwarta!" << endl;

    while (true) {
        bool podjeto_akcje = false;

        if (stan->kierunek_ruchu_kladka == DIR_NONE || stan->kierunek_ruchu_kladka == DIR_ENTERING) {

            if (stan->bilety_sprzedane_t1 > stan->osoby_trasa1 && stan->osoby_na_kladce < K) {
                stan->osoby_na_kladce++;
                stan->kierunek_ruchu_kladka = DIR_ENTERING;
                cout << ">>> [WEJSCIE T1] Na most. (Most: " << stan->osoby_na_kladce << "/" << K << ")" << endl;
                usleep(300000);
                stan->osoby_na_kladce--;
                stan->osoby_trasa1++;
                cout << "    [JASKINIA T1] Dotarł. (Stan: " << stan->osoby_trasa1 << "/" << N1 << ")" << endl;
                if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;
                podjeto_akcje = true;
            }
            else if (stan->bilety_sprzedane_t2 > stan->osoby_trasa2 && stan->osoby_na_kladce < K) {
                stan->osoby_na_kladce++;
                stan->kierunek_ruchu_kladka = DIR_ENTERING;
                cout << ">>> [WEJSCIE T2] Na most. (Most: " << stan->osoby_na_kladce << "/" << K << ")" << endl;
                usleep(300000);
                stan->osoby_na_kladce--;
                stan->osoby_trasa2++;
                cout << "    [JASKINIA T2] Dotarł. (Stan: " << stan->osoby_trasa2 << "/" << N2 << ")" << endl;
                if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;
                podjeto_akcje = true;
            }
        }

        if (!podjeto_akcje && (stan->kierunek_ruchu_kladka == DIR_NONE || stan->kierunek_ruchu_kladka == DIR_LEAVING)) {

            if (stan->osoby_trasa1 > 0 && stan->osoby_na_kladce < K && (rand() % 10 < 3)) {
                stan->osoby_trasa1--;
                stan->osoby_na_kladce++;
                stan->kierunek_ruchu_kladka = DIR_LEAVING;

                cout << "<<< [WYJSCIE T1] Wchodzi na most. (Most: " << stan->osoby_na_kladce << "/" << K << ")" << endl;

                usleep(300000);

                stan->osoby_na_kladce--;
                stan->bilety_sprzedane_t1--;

                cout << "    [KONIEC T1] Turysta opuścił jaskinię. Bilet zwolniony." << endl;

                if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;
                podjeto_akcje = true;
            }

            else if (stan->osoby_trasa2 > 0 && stan->osoby_na_kladce < K && (rand() % 10 < 3)) {
                stan->osoby_trasa2--;
                stan->osoby_na_kladce++;
                stan->kierunek_ruchu_kladka = DIR_LEAVING;

                cout << "<<< [WYJSCIE T2] Wchodzi na most. (Most: " << stan->osoby_na_kladce << "/" << K << ")" << endl;

                usleep(300000);

                stan->osoby_na_kladce--;
                stan->bilety_sprzedane_t2--;

                cout << "    [KONIEC T2] Turysta opuścił jaskinię. Bilet zwolniony." << endl;

                if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;
                podjeto_akcje = true;
            }
        }

        if (!podjeto_akcje) usleep(100000);
    }

    if (shmdt(stan) == -1) perror("[PRZEWODNIK] shmdt");
    return 0;
}
