#include "common.hpp"
#include <iostream>

using namespace std;

int main() {
    cout << "[KASJER] Start..." << endl;

    key_t key = ftok(FTOK_FILE, SHM_ID);
    int shm_id = shmget(key, sizeof(JaskiniaStan), 0666);
    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);

    key = ftok(FTOK_FILE, SEM_ID);
    int sem_id = semget(key, 1, 0666);

    key = ftok(FTOK_FILE, MSG_ID);
    int msg_id = msgget(key, IPC_CREAT | 0666);

    if (shm_id == -1 || sem_id == -1 || msg_id == -1) {
        perror("[KASJER] Blad inicjalizacji");
        return 1;
    }

    Wiadomosc msg;
    while (true) {
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), 1, 0) == -1) break;

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
        msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0);
    }
    return 0;
}