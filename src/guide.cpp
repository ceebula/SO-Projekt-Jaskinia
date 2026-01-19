#include "common.hpp"
#include <iostream>
#include <cstdlib>

using namespace std;

static JaskiniaStan* stan = nullptr;
static int trasa = 0;

static void alarm_t1(int) {
    if (stan) stan->alarm_t1 = 1;
    cout << "[PRZEWODNIK T1] Alarm! Wstrzymuję wejścia." << endl;
}

static void alarm_t2(int) {
    if (stan) stan->alarm_t2 = 1;
    cout << "[PRZEWODNIK T2] Alarm! Wstrzymuję wejścia." << endl;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        cerr << "[PRZEWODNIK] Brak parametru trasy (1/2)" << endl;
        return 1;
    }

    trasa = atoi(argv[1]);
    if (trasa != 1 && trasa != 2) {
        cerr << "[PRZEWODNIK] Zły parametr trasy: " << argv[1] << endl;
        return 1;
    }

    if (trasa == 1) {
        signal(SIGUSR1, alarm_t1);
        signal(SIGUSR2, SIG_IGN);
    } else {
        signal(SIGUSR2, alarm_t2);
        signal(SIGUSR1, SIG_IGN);
    }

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) die_perror("ftok");

    int shm_id = shmget(key, sizeof(JaskiniaStan), 0600);
    if (shm_id == -1) die_perror("shmget");

    stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) die_perror("shmat");

    cout << (trasa == 1 ? "[PRZEWODNIK T1] Gotowy" : "[PRZEWODNIK T2] Gotowy") << endl;

    while (true) {
        if (trasa == 1) {
            if (!stan->alarm_t1 &&
                stan->kierunek_ruchu_kladka == DIR_NONE &&
                stan->bilety_sprzedane_t1 > stan->osoby_trasa1 &&
                stan->osoby_na_kladce < K) {

                stan->osoby_na_kladce++;
                stan->kierunek_ruchu_kladka = DIR_ENTERING;

                usleep(300000);

                stan->osoby_na_kladce--;
                stan->osoby_trasa1++;

                stan->kierunek_ruchu_kladka = DIR_NONE;
            }
        } else {
            if (!stan->alarm_t2 &&
                stan->kierunek_ruchu_kladka == DIR_NONE &&
                stan->bilety_sprzedane_t2 > stan->osoby_trasa2 &&
                stan->osoby_na_kladce < K) {

                stan->osoby_na_kladce++;
                stan->kierunek_ruchu_kladka = DIR_ENTERING;

                usleep(300000);

                stan->osoby_na_kladce--;
                stan->osoby_trasa2++;

                stan->kierunek_ruchu_kladka = DIR_NONE;
            }
        }

        usleep(100000);
    }
}
