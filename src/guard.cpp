#include "common.hpp"
#include <iostream>
#include <cstdlib>

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

    while (!g_terminated) {
        for (int i = 0; i < 50 && !g_terminated; i++) {
            usleep(100000);
        }
        if (g_terminated) break;

        int which = (rand() % 2) + 1;
        int sig = (which == 1) ? SIGUSR1 : SIGUSR2;

        lock_sem(sem_id);
        pid_t target = (which == 1) ? stan->przewodnik_t1_pid : stan->przewodnik_t2_pid;
        unlock_sem(sem_id);

        if (target <= 0) continue;

        if (which == 1) {
            cout << "[STRAZNIK] Wysylam sygnal T1 do pid=" << target << endl;
            logf_simple("STRAZNIK", "Wysylam sygnal T1");
        } else {
            cout << "[STRAZNIK] Wysylam sygnal T2 do pid=" << target << endl;
            logf_simple("STRAZNIK", "Wysylam sygnal T2");
        }

        if (kill(target, sig) == -1) {
            if (errno != ESRCH) perror("kill");
        }
    }

    if (shmdt(stan) == -1) perror("shmdt");
    return 0;
}
