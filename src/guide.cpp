#include "common.hpp"
#include <iostream>
#include <cstdlib>

using namespace std;

static JaskiniaStan* stan = nullptr;
static int trasa = 0;
static int sem_id = -1;

static void alarm_t1(int) {
    if (!stan) return;
    lock_sem(sem_id);
    stan->alarm_t1 = 1;
    stan->alarm_do_t1 = time(NULL) + ALARM_SECONDS;
    unlock_sem(sem_id);
    cout << "[PRZEWODNIK T1] Alarm! Blokada wejsc przez " << ALARM_SECONDS << "s" << endl;
}

static void alarm_t2(int) {
    if (!stan) return;
    lock_sem(sem_id);
    stan->alarm_t2 = 1;
    stan->alarm_do_t2 = time(NULL) + ALARM_SECONDS;
    unlock_sem(sem_id);
    cout << "[PRZEWODNIK T2] Alarm! Blokada wejsc przez " << ALARM_SECONDS << "s" << endl;
}

static bool alarm_aktywny_locked(time_t now) {
    if (trasa == 1) {
        if (stan->alarm_t1 && now >= stan->alarm_do_t1) stan->alarm_t1 = 0;
        return stan->alarm_t1 != 0;
    } else {
        if (stan->alarm_t2 && now >= stan->alarm_do_t2) stan->alarm_t2 = 0;
        return stan->alarm_t2 != 0;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    trasa = atoi(argv[1]);
    if (trasa != 1 && trasa != 2) return 1;

    if (trasa == 1) { signal(SIGUSR1, alarm_t1); signal(SIGUSR2, SIG_IGN); }
    else { signal(SIGUSR2, alarm_t2); signal(SIGUSR1, SIG_IGN); }

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) die_perror("ftok SHM");
    int shm_id = shmget(key, sizeof(JaskiniaStan), 0600);
    if (shm_id == -1) die_perror("shmget");
    stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) die_perror("shmat");

    key = ftok(FTOK_FILE, SEM_ID);
    if (key == -1) die_perror("ftok SEM");
    sem_id = semget(key, 1, 0600);
    if (sem_id == -1) die_perror("semget");

    cout << (trasa == 1 ? "[PRZEWODNIK T1] Gotowy" : "[PRZEWODNIK T2] Gotowy") << endl;

    while (true) {
        time_t now = time(NULL);

        lock_sem(sem_id);
        bool koniec = (stan->end_time != 0 && now >= stan->end_time);
        bool alarm = alarm_aktywny_locked(now);

        int* ocz = (trasa == 1) ? &stan->oczekujacy_t1 : &stan->oczekujacy_t2;
        int* wjask = (trasa == 1) ? &stan->osoby_trasa1 : &stan->osoby_trasa2;
        int* bilety = (trasa == 1) ? &stan->bilety_sprzedane_t1 : &stan->bilety_sprzedane_t2;

        bool most_wolny = (stan->osoby_na_kladce < K);
        bool kier_none = (stan->kierunek_ruchu_kladka == DIR_NONE);

        bool zrobiono = false;

        // WYJSCIE: losowo wypuszczamy z jaskini
        if (*wjask > 0 && most_wolny && (kier_none || stan->kierunek_ruchu_kladka == DIR_LEAVING)) {
            if ((rand() % 100) < 25 || koniec) {
                stan->kierunek_ruchu_kladka = DIR_LEAVING;
                stan->osoby_na_kladce++;
                (*wjask)--;
                cout << "[PRZEWODNIK T" << trasa << "] WYJSCIE na kladke | w_jaskini="
                    << *wjask << " | kladka=" << stan->osoby_na_kladce << "/" << K << endl;

                unlock_sem(sem_id);

                usleep(300000);

                lock_sem(sem_id);
                stan->osoby_na_kladce--;
                (*bilety)--;
                cout << "[PRZEWODNIK T" << trasa << "] OPUSCIL jaskinie | bilety="
                    << *bilety << " | kladka=" << stan->osoby_na_kladce << endl;

                if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;
                zrobiono = true;
            }
        }

        // WEJSCIE: bierzemy z kolejki do jaskini (blokowane alarmem)
        if (!zrobiono && !alarm && *ocz > 0 && most_wolny && (kier_none || stan->kierunek_ruchu_kladka == DIR_ENTERING)) {
            stan->kierunek_ruchu_kladka = DIR_ENTERING;
            stan->osoby_na_kladce++;
            (*ocz)--;
            cout << "[PRZEWODNIK T" << trasa << "] WEJSCIE na kladke | kolejka="
                << *ocz << " | kladka=" << stan->osoby_na_kladce << "/" << K << endl;

            unlock_sem(sem_id);

            usleep(300000);

            lock_sem(sem_id);
            stan->osoby_na_kladce--;
            (*wjask)++;
            cout << "[PRZEWODNIK T" << trasa << "] DOTARL do jaskini | w_jaskini="
                << *wjask << " | kladka=" << stan->osoby_na_kladce << endl;

            if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;
            zrobiono = true;
        }

        // Jeśli koniec symulacji i nic nie ma do roboty, możemy zejść
        if (!zrobiono && koniec) {
            bool pusto = (stan->osoby_na_kladce == 0 &&
                          stan->oczekujacy_t1 == 0 && stan->oczekujacy_t2 == 0 &&
                          stan->osoby_trasa1 == 0 && stan->osoby_trasa2 == 0);
            unlock_sem(sem_id);
            if (pusto) break;
        } else {
            unlock_sem(sem_id);
        }

        usleep(100000);
    }

    if (shmdt(stan) == -1) perror("shmdt");
    return 0;
}
