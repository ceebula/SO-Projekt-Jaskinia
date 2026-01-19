#include "common.hpp"
#include <iostream>
#include <cstdlib>
#include <ctime>

using namespace std;

int main() {
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    int pid = getpid();
    int wiek = rand() % 90;
    int powrot = (rand() % 10 == 0);
    int trasa = (rand() % 2) + 1;

    cout << "[TURYSTA " << pid << "] Wiek=" << wiek;
    if (powrot) cout << " (POWROT)";
    cout << endl;

    if (wiek < 3 || wiek > 75) {
        cout << "[TURYSTA " << pid << "] Brak wstepu" << endl;
        return 0;
    }

    if (wiek < 8 && trasa == 1) {
        cout << "[TURYSTA " << pid << "] Zbyt mlody na T1, wybieram T2" << endl;
        trasa = 2;
    }

    key_t key = ftok(FTOK_FILE, MSG_ID);
    if (key == -1) die_perror("ftok MSG");
    int msg_id = msgget(key, 0600);
    if (msg_id == -1) die_perror("msgget");

    Wiadomosc msg{};
    msg.mtype = 1;
    msg.id_nadawcy = pid;
    msg.typ_biletu = trasa;
    msg.wiek = wiek;
    msg.powrot = powrot;

    if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) die_perror("msgsnd");
    if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), pid, 0) == -1) die_perror("msgrcv");

    if (msg.odpowiedz)
        cout << "[TURYSTA " << pid << "] Bilet OK" << endl;
    else
        cout << "[TURYSTA " << pid << "] Brak biletu" << endl;

    return 0;
}
