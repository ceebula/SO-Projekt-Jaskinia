#include "common.hpp"
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <sys/wait.h>

using namespace std;

static volatile sig_atomic_t g_terminated = 0;
static void handle_term(int) { g_terminated = 1; }

static void run_visitor(int wiek, int trasa, int group_size, pid_t pids[2], int sync_fd);

int main(int argc, char** argv) {
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGTERM, handle_term);

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    int wiek = 1 + (rand() % 80);
    int trasa = (rand() % 2) + 1;
    int group_size = 1;
    pid_t pids[2] = {getpid(), 0};
    int sync_fd = -1;

    if (argc >= 4 && string(argv[1]) == "opiekun") {
        wiek = 30;
        trasa = 2;
        sync_fd = atoi(argv[2]);
        pid_t child_pid = atoi(argv[3]);
        pids[0] = child_pid;
        pids[1] = getpid();
        group_size = 2;
        cout << "[OPIEKUN " << getpid() << "] Idę z dzieckiem pid=" << child_pid << endl;
        run_visitor(wiek, trasa, group_size, pids, sync_fd);
        return 0;
    }

    cout << "[TURYSTA " << getpid() << "] Wiek=" << wiek << endl;

    if (wiek < 8) {
        trasa = 2;
        group_size = 2;

        int pipefd[2];
        if (pipe(pipefd) == -1) die_perror("pipe");

        pid_t guardian = fork();
        if (guardian == -1) die_perror("fork opiekun");

        if (guardian == 0) {
            close(pipefd[1]);
            char fd_str[16], pid_str[16];
            snprintf(fd_str, sizeof(fd_str), "%d", pipefd[0]);
            snprintf(pid_str, sizeof(pid_str), "%d", getppid());
            execl("./Zwiedzajacy", "Zwiedzajacy", "opiekun", fd_str, pid_str, (char*)NULL);
            perror("execl opiekun");
            _exit(127);
        }

        close(pipefd[0]);
        sync_fd = pipefd[1];
        pids[0] = getpid();
        pids[1] = guardian;

        cout << "[TURYSTA " << getpid() << "] Dziecko <8 z opiekunem pid=" << guardian << " - tylko T2" << endl;
    } else if (wiek > 75) {
        trasa = 2;
        cout << "[TURYSTA " << getpid() << "] Senior 76+ - tylko T2" << endl;
    } else {
        cout << "[TURYSTA " << getpid() << "] Chce bilet na T" << trasa << endl;
    }

    run_visitor(wiek, trasa, group_size, pids, sync_fd);

    if (wiek < 8) {
        if (sync_fd != -1) close(sync_fd);
        waitpid(-1, NULL, 0);
    }

    return 0;
}

static void run_visitor(int wiek, int trasa, int group_size, pid_t pids[2], int sync_fd) {
    pid_t mypid = getpid();
    bool is_guardian = (pids[1] == mypid);

    if (is_guardian) {
        char buf;
        if (read(sync_fd, &buf, 1) <= 0) {
            close(sync_fd);
            return;
        }
        close(sync_fd);

        key_t key = ftok(FTOK_FILE, MSG_ID);
        int msg_id = msgget(key, 0600);
        if (msg_id == -1) return;

        MsgEnter msgEnter{};
        if (msgrcv(msg_id, &msgEnter, sizeof(msgEnter) - sizeof(long), MSG_ENTER_BASE + mypid, 0) == -1) {
            return;
        }

        if (msgEnter.trasa == -1) {
            cout << "[OPIEKUN " << mypid << "] Kolejka anulowana - wychodzę" << endl;
            return;
        }

        cout << "[OPIEKUN " << mypid << "] Wchodzę na trasę T2" << endl;
        usleep((useconds_t)T2_MS * 1000);
        cout << "[OPIEKUN " << mypid << "] Skończyłem trasę T2" << endl;

        MsgExit msgExit{};
        msgExit.mtype = MSG_EXIT_T2;
        msgExit.pid = mypid;
        msgExit.group_size = 1;
        msgsnd(msg_id, &msgExit, sizeof(msgExit) - sizeof(long), 0);
        return;
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
    msg.pids[0] = pids[0];
    msg.pids[1] = pids[1];

    if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        if (g_terminated) return;
        die_perror("msgsnd kasjer");
    }
    if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), mypid, 0) == -1) {
        if (g_terminated || errno == EINTR) return;
        die_perror("msgrcv kasjer");
    }

    if (!msg.odpowiedz) {
        cout << "[TURYSTA " << mypid << "] Brak biletu - wychodzę" << endl;
        return;
    }

    cout << "[TURYSTA " << mypid << "] Bilet OK, czekam w kolejce T" << trasa << endl;

    if (sync_fd != -1) {
        char c = 1;
        if (write(sync_fd, &c, 1) == -1) perror("write sync");
    }

    MsgEnter msgEnter{};
    if (msgrcv(msg_id, &msgEnter, sizeof(msgEnter) - sizeof(long), MSG_ENTER_BASE + mypid, 0) == -1) {
        if (g_terminated || errno == EINTR || errno == EIDRM) {
            cout << "[TURYSTA " << mypid << "] Przerwano - wychodzę" << endl;
            return;
        }
        die_perror("msgrcv enter");
    }

    if (msgEnter.trasa == -1) {
        cout << "[TURYSTA " << mypid << "] Kolejka anulowana - wychodzę" << endl;
        logf_simple("TURYSTA", "Kolejka anulowana");
        return;
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
    msgExit.group_size = 1;

    if (msgsnd(msg_id, &msgExit, sizeof(msgExit) - sizeof(long), 0) == -1) {
        if (errno != EINTR && errno != EIDRM) {
            perror("msgsnd exit");
        }
    }

    if (group_size == 1 && (rand() % 10) == 0) {
        int nowa_trasa = (trasa == 1) ? 2 : 1;
        if (wiek > 75 && nowa_trasa == 1) return;

        cout << "[TURYSTA " << mypid << "] Powrot! Chce bilet na T" << nowa_trasa << " (priorytet)" << endl;

        Wiadomosc msg2{};
        msg2.mtype = MSG_KASJER;
        msg2.id_nadawcy = mypid;
        msg2.typ_biletu = nowa_trasa;
        msg2.wiek = wiek;
        msg2.powrot = 1;
        msg2.group_size = 1;
        msg2.pids[0] = mypid;
        msg2.pids[1] = 0;

        if (msgsnd(msg_id, &msg2, sizeof(msg2) - sizeof(long), 0) == -1) return;
        if (msgrcv(msg_id, &msg2, sizeof(msg2) - sizeof(long), mypid, 0) == -1) return;

        if (!msg2.odpowiedz) {
            cout << "[TURYSTA " << mypid << "] Powrot odmowiony" << endl;
            return;
        }

        cout << "[TURYSTA " << mypid << "] Powrot OK, czekam w kolejce T" << nowa_trasa << endl;
        logf_simple("TURYSTA", "Powrot: bilet OK");

        MsgEnter msgEnter2{};
        if (msgrcv(msg_id, &msgEnter2, sizeof(msgEnter2) - sizeof(long), MSG_ENTER_BASE + mypid, 0) == -1) return;
        if (msgEnter2.trasa == -1) return;

        cout << "[TURYSTA " << mypid << "] Wchodzę na trasę T" << nowa_trasa << " (powrot)" << endl;
        logf_simple("TURYSTA", "Powrot: wejście na trasę");

        int czas2 = (nowa_trasa == 1) ? T1_MS : T2_MS;
        usleep((useconds_t)czas2 * 1000);

        cout << "[TURYSTA " << mypid << "] Skończyłem trasę T" << nowa_trasa << " (powrot)" << endl;
        logf_simple("TURYSTA", "Powrot: zakończenie trasy");

        MsgExit msgExit2{};
        msgExit2.mtype = (nowa_trasa == 1) ? MSG_EXIT_T1 : MSG_EXIT_T2;
        msgExit2.pid = mypid;
        msgExit2.group_size = 1;
        msgsnd(msg_id, &msgExit2, sizeof(msgExit2) - sizeof(long), 0);
    }
}
