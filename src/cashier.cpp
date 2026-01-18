#include "common.hpp"
#include <iostream>

using namespace std;

int main() {
    cout << "[KASJER] Start..." << endl;

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) die_perror("[KASJER] ftok SHM");
    int shm_id = shmget(key, sizeof(JaskiniaStan), 0600);
    if (shm_id == -1) die_perror("[KASJER] shmget");
    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) die_perror("[KASJER] shmat");

    key = ftok(FTOK_FILE, SEM_ID);
    if (key == -1) die_perror("[KASJER] ftok SEM");
    int sem_id = semget(key, 1, 0600);
    if (sem_id == -1) die_perror("[KASJER] semget");

    key = ftok(FTOK_FILE, MSG_ID);
    if (key == -1) die_perror("[KASJER] ftok MSG");
    int msg_id = msgget(key, 0600);
    if (msg_id == -1) die_perror("[KASJER] msgget");

    Wiadomosc msg;
    while (true) {
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), 1, 0) == -1) {
            if (errno == EINTR) continue;
            perror("[KASJER] msgrcv");
            break;
        }

        bool sprzedano = false;

        lock_sem(sem_id);

        if (msg.typ_biletu == 1) {
            if (stan->bilety_sprzedane_t1 < N1) {
                stan->bilety_sprzedane_t1++;
                sprzedano = true;
                cout << "[KASJER] Sprzedano bilet T1 dla " << msg.id_nadawcy
                     << ". Stan: " << stan->bilety_sprzedane_t1 << "/" << N1 << endl;
            }
        } else {
            if (stan->bilety_sprzedane_t2 < N2) {
                stan->bilety_sprzedane_t2++;
                sprzedano = true;
                cout << "[KASJER] Sprzedano bilet T2 dla " << msg.id_nadawcy
                     << ". Stan: " << stan->bilety_sprzedane_t2 << "/" << N2 << endl;
            }
        }

        unlock_sem(sem_id);

        msg.mtype = msg.id_nadawcy;
        msg.odpowiedz = sprzedano;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            if (errno == EINTR) continue;
            perror("[KASJER] msgsnd");
            break;
        }
    }

    if (shmdt(stan) == -1) perror("[KASJER] shmdt");
    return 0;
}
