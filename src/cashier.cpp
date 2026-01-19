#include "common.hpp"
#include <iostream>

using namespace std;

int main() {
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) die_perror("ftok SHM");
    int shm_id = shmget(key, sizeof(JaskiniaStan), 0600);
    if (shm_id == -1) die_perror("shmget");
    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) die_perror("shmat");

    key = ftok(FTOK_FILE, SEM_ID);
    if (key == -1) die_perror("ftok SEM");
    int sem_id = semget(key, 1, 0600);
    if (sem_id == -1) die_perror("semget");

    key = ftok(FTOK_FILE, MSG_ID);
    if (key == -1) die_perror("ftok MSG");
    int msg_id = msgget(key, 0600);
    if (msg_id == -1) die_perror("msgget");

    Wiadomosc msg;
    while (true) {
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), 1, 0) == -1) {
            if (errno == EINTR) continue;
            perror("msgrcv");
            break;
        }

        bool ok = true;
        if (msg.wiek < 3 || msg.wiek > 75) ok = false;
        if (msg.wiek < 8 && msg.typ_biletu == 1) ok = false;

        lock_sem(sem_id);

        if (ok) {
            if (msg.typ_biletu == 1) {
                if (stan->bilety_sprzedane_t1 < N1) stan->bilety_sprzedane_t1++;
                else ok = false;
            } else {
                if (stan->bilety_sprzedane_t2 < N2) stan->bilety_sprzedane_t2++;
                else ok = false;
            }
        }

        unlock_sem(sem_id);

        msg.mtype = msg.id_nadawcy;
        msg.odpowiedz = ok;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            if (errno == EINTR) continue;
            perror("msgsnd");
            break;
        }
    }

    if (shmdt(stan) == -1) perror("shmdt");
    return 0;
}
