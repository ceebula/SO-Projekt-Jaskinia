#include "common.hpp"
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <sys/wait.h>

using namespace std;

static void spawn_guardian(int child_pid, int child_age) {
    pid_t p = fork();
    if (p == -1) die_perror("fork guardian");
    if (p == 0) {
        char pidbuf[32];
        char agebuf[32];
        snprintf(pidbuf, sizeof(pidbuf), "%d", child_pid);
        snprintf(agebuf, sizeof(agebuf), "%d", child_age);
        execl("./Zwiedzajacy", "Zwiedzajacy", "opiekun", pidbuf, agebuf, (char*)NULL);
        perror("execl guardian");
        _exit(127);
    }
}

int main(int argc, char** argv) {
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    if (argc >= 2 && string(argv[1]) == "opiekun") {
        int child_pid = (argc >= 3) ? atoi(argv[2]) : -1;
        int child_age = (argc >= 4) ? atoi(argv[3]) : -1;
        cout << "[OPIEKUN " << getpid() << "] Ide z dzieckiem pid=" << child_pid << " wiek=" << child_age << endl;
        return 0;
    }

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    int pid = getpid();
    int wiek = rand() % 90;
    int trasa = (rand() % 2) + 1;

    cout << "[TURYSTA " << pid << "] Wiek=" << wiek << endl;

    if (wiek < 3 || wiek > 75) {
        cout << "[TURYSTA " << pid << "] Brak wstepu" << endl;
        return 0;
    }

    int group_size = 1;

    if (wiek < 8) {
        trasa = 2;
        group_size = 2;
        spawn_guardian(pid, wiek);
        cout << "[TURYSTA " << pid << "] Dziecko - ide z opiekunem (grupa=2) tylko T2" << endl;
    } else {
        if (trasa == 1) {
            cout << "[TURYSTA " << pid << "] Chce bilet na T1" << endl;
        } else {
            cout << "[TURYSTA " << pid << "] Chce bilet na T2" << endl;
        }
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
    msg.powrot = 0;
    msg.group_size = group_size;

    if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) die_perror("msgsnd");
    if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), pid, 0) == -1) die_perror("msgrcv");

    if (!msg.odpowiedz) {
        cout << "[TURYSTA " << pid << "] Brak biletu" << endl;
        return 0;
    }

    cout << "[TURYSTA " << pid << "] Bilet OK, dolaczam do kolejki T" << trasa
         << " (grupa=" << group_size << ")" << endl;

    return 0;
}
