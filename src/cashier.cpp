#include "common.hpp"
#include <iostream>

using namespace std;

int main() {
    key_t key = ftok(FTOK_FILE, SHM_ID);
    int shm_id = shmget(key, sizeof(JaskiniaStan), 0600);
    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);

    key = ftok(FTOK_FILE, SEM_ID);
    int sem_id = semget(key, 1, 0600);

    key = ftok(FTOK_FILE, MSG_ID);
    int msg_id = msgget(key, 0600);

    Wiadomosc msg;
    while (true) {
        msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), 1, 0);

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
        msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0);
    }
}
