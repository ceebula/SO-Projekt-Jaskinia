#include "common.hpp"
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <sys/wait.h>

using namespace std;

static volatile sig_atomic_t g_terminated = 0;

static void handle_term(int) {
    g_terminated = 1;
}

int main() {
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGTERM, handle_term);

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    pid_t mypid = getpid();
    int wiek = 1 + (rand() % 80);
    int trasa = (rand() % 2) + 1;

    cout << "[TURYSTA " << mypid << "] Wiek=" << wiek << endl;

    int group_size = 1;

    if (wiek < 8) {
        trasa = 2;
        group_size = 1;
        cout << "[TURYSTA " << mypid << "] Dziecko <8 - tylko T2" << endl;
    } else if (wiek > 75) {
        trasa = 2;
        cout << "[TURYSTA " << mypid << "] Senior 76+ - tylko T2" << endl;
    } else {
        if (trasa == 1) cout << "[TURYSTA " << mypid << "] Chce bilet na T1" << endl;
        else cout << "[TURYSTA " << mypid << "] Chce bilet na T2" << endl;
    }

    key_t key = ftok(FTOK_FILE, MSG_ID);
    if (key == -1) die_perror("ftok MSG");
    int msg_id = msgget(key, 0600);
    if (msg_id == -1) die_perror("msgget");

    Wiadomosc msg{};
    msg.mtype = MSG_KASJER;
    msg.id_nadawcy = mypid;
    msg.typ_biletu = trasa;
    msg.wiek = wiek;
    msg.powrot = 0;
    msg.group_size = group_size;
    msg.pids[0] = mypid;
    msg.pids[1] = 0;

    if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        if (g_terminated) return 0;
        die_perror("msgsnd kasjer");
    }
    if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), mypid, 0) == -1) {
        if (g_terminated || errno == EINTR) return 0;
        die_perror("msgrcv kasjer");
    }

    if (!msg.odpowiedz) {
        cout << "[TURYSTA " << mypid << "] Brak biletu - wychodzę" << endl;
        return 0;
    }

    cout << "[TURYSTA " << mypid << "] Bilet OK, czekam w kolejce T" << trasa << endl;

    MsgEnter msgEnter{};
    if (msgrcv(msg_id, &msgEnter, sizeof(msgEnter) - sizeof(long), MSG_ENTER_BASE + mypid, 0) == -1) {
        if (g_terminated || errno == EINTR || errno == EIDRM) {
            cout << "[TURYSTA " << mypid << "] Przerwano - wychodzę" << endl;
            return 0;
        }
        die_perror("msgrcv enter");
    }

    cout << "[TURYSTA " << mypid << "] Wchodzę na trasę T" << trasa << endl;
    logf_simple("TURYSTA", "Wejście na trasę");

    int czas_ms = (trasa == 1) ? T1_MS : T2_MS;
    usleep((useconds_t)czas_ms * 1000);

    cout << "[TURYSTA " << mypid << "] Skończyłem trasę T" << trasa << ", wychodzę" << endl;
    logf_simple("TURYSTA", "Zakończenie trasy");

    MsgExit msgExit{};
    msgExit.mtype = (trasa == 1) ? MSG_EXIT_T1 : MSG_EXIT_T2;
    msgExit.pid = mypid;
    msgExit.group_size = group_size;

    if (msgsnd(msg_id, &msgExit, sizeof(msgExit) - sizeof(long), 0) == -1) {
        if (errno != EINTR && errno != EIDRM) {
            perror("msgsnd exit");
        }
    }

    return 0;
}
