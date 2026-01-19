#include "common.hpp"
#include <iostream>
#include <unistd.h>

using namespace std;

JaskiniaStan* stan = nullptr;

void sig_t1(int) {
    stan->alarm_t1 = 1;
    cout << "[PRZEWODNIK] Alarm na trasie T1!" << endl;
}

void sig_t2(int) {
    stan->alarm_t2 = 1;
    cout << "[PRZEWODNIK] Alarm na trasie T2!" << endl;
}

int main() {
    signal(SIGUSR1, sig_t1);
    signal(SIGUSR2, sig_t2);

    key_t key = ftok(FTOK_FILE, SHM_ID);
    int shm_id = shmget(key, sizeof(JaskiniaStan), 0600);
    if (shm_id == -1) die_perror("shmget");

    stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) die_perror("shmat");

    cout << "[PRZEWODNIK] Gotowy" << endl;

    while (true) {
        if (stan->kierunek_ruchu_kladka == DIR_NONE &&
            stan->alarm_t1 == 0 &&
            stan->bilety_sprzedane_t1 > stan->osoby_trasa1 &&
            stan->osoby_na_kladce < K) {

            stan->osoby_na_kladce++;
            stan->kierunek_ruchu_kladka = DIR_ENTERING;
            usleep(300000);
            stan->osoby_na_kladce--;
            stan->osoby_trasa1++;
            stan->kierunek_ruchu_kladka = DIR_NONE;
        }

        if (stan->kierunek_ruchu_kladka == DIR_NONE &&
            stan->alarm_t2 == 0 &&
            stan->bilety_sprzedane_t2 > stan->osoby_trasa2 &&
            stan->osoby_na_kladce < K) {

            stan->osoby_na_kladce++;
            stan->kierunek_ruchu_kladka = DIR_ENTERING;
            usleep(300000);
            stan->osoby_na_kladce--;
            stan->osoby_trasa2++;
            stan->kierunek_ruchu_kladka = DIR_NONE;
        }

        usleep(100000);
    }
}
