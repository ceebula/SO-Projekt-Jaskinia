#include "common.hpp"
#include <iostream>

using namespace std;

int main() {
    srand(getpid());
    int pid = getpid();

    key_t key = ftok(FTOK_FILE, MSG_ID);
    if (key == -1) die_perror("[TURYSTA] ftok MSG");
    int msg_id = msgget(key, 0600);
    if (msg_id == -1) die_perror("[TURYSTA] msgget");

    int trasa = (rand() % 2) + 1;
    cout << "[TURYSTA " << pid << "] Chce bilet na trase " << trasa << endl;

    Wiadomosc msg;
    msg.mtype = 1;
    msg.id_nadawcy = pid;
    msg.typ_biletu = trasa;

    if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) die_perror("[TURYSTA] msgsnd");

    if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), pid, 0) == -1) die_perror("[TURYSTA] msgrcv");

    if (msg.odpowiedz) {
        cout << "[TURYSTA " << pid << "] Kupilem bilet! Czekam na wejscie..." << endl;
        sleep(5);
        cout << "[TURYSTA " << pid << "] Rezygnuje i wracam do domu." << endl;
    } else {
        cout << "[TURYSTA " << pid << "] Brak biletow, odchodze." << endl;
    }

    return 0;
}
