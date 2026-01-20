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

static int dequeue_group_locked(GroupItem& out) {
    if (trasa == 1) {
        if (stan->q_t1_prio.count > 0) return q_pop(stan->q_t1_prio, out);
        if (stan->q_t1.count > 0) return q_pop(stan->q_t1, out);
        return -1;
    } else {
        if (stan->q_t2_prio.count > 0) return q_pop(stan->q_t2_prio, out);
        if (stan->q_t2.count > 0) return q_pop(stan->q_t2, out);
        return -1;
    }
}

static int waiting_count_locked() {
    if (trasa == 1) return stan->q_t1.count + stan->q_t1_prio.count;
    return stan->q_t2.count + stan->q_t2_prio.count;
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

        int* wjask = (trasa == 1) ? &stan->osoby_trasa1 : &stan->osoby_trasa2;
        int* bilety = (trasa == 1) ? &stan->bilety_sprzedane_t1 : &stan->bilety_sprzedane_t2;

        int ocz = waiting_count_locked();
        if (trasa == 1) stan->oczekujacy_t1 = ocz;
        else stan->oczekujacy_t2 = ocz;

        bool kier_none = (stan->kierunek_ruchu_kladka == DIR_NONE);
        bool zrobiono = false;

        if (*wjask > 0 && (stan->osoby_na_kladce + 1 <= K) &&
            (kier_none || stan->kierunek_ruchu_kladka == DIR_LEAVING)) {

            if ((rand() % 100) < 25 || koniec) {
                int group_size = 1;

                stan->kierunek_ruchu_kladka = DIR_LEAVING;
                stan->osoby_na_kladce += group_size;
                (*wjask) -= group_size;
                if (*wjask < 0) *wjask = 0;
                unlock_sem(sem_id);

                usleep(300000);

                lock_sem(sem_id);
                stan->osoby_na_kladce -= group_size;
                (*bilety) -= group_size;
                if (*bilety < 0) *bilety = 0;
                if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;

                cout << "[PRZEWODNIK T" << trasa << "] OPUSCIL jaskinie | bilety=" << *bilety
                     << " | kladka=" << stan->osoby_na_kladce << endl;

                zrobiono = true;
            }
        }

        if (!zrobiono && !alarm) {
            GroupItem it{};
            bool jest = (dequeue_group_locked(it) == 0);
            int group_size = (it.group_size > 0) ? it.group_size : 1;

            if (jest &&
                (stan->osoby_na_kladce + group_size <= K) &&
                (kier_none || stan->kierunek_ruchu_kladka == DIR_ENTERING)) {

                stan->kierunek_ruchu_kladka = DIR_ENTERING;
                stan->osoby_na_kladce += group_size;

                int kolejka_po = waiting_count_locked();
                unlock_sem(sem_id);

                cout << "[PRZEWODNIK T" << trasa << "] WEJSCIE na kladke | kolejka="
                     << kolejka_po << " | kladka=" << stan->osoby_na_kladce << "/" << K
                     << " | grupa=" << group_size << endl;

                usleep(300000);

                lock_sem(sem_id);
                stan->osoby_na_kladce -= group_size;
                (*wjask) += group_size;
                if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;

                cout << "[PRZEWODNIK T" << trasa << "] DOTARL do jaskini | w_jaskini=" << *wjask
                     << " | kladka=" << stan->osoby_na_kladce << endl;

                zrobiono = true;
            }
        }

        if (!zrobiono && koniec) {
            bool pusto = (stan->osoby_na_kladce == 0 &&
                          (stan->q_t1.count + stan->q_t1_prio.count) == 0 &&
                          (stan->q_t2.count + stan->q_t2_prio.count) == 0 &&
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
