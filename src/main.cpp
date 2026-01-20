#include "common.hpp"
#include <iostream>
#include <sys/wait.h>

using namespace std;

int shm_id = -1, sem_id = -1, msg_id = -1;

static void cleanup(int) {
    kill(0, SIGTERM);
    if (shm_id != -1) shmctl(shm_id, IPC_RMID, NULL);
    if (sem_id != -1) semctl(sem_id, 0, IPC_RMID);
    if (msg_id != -1) msgctl(msg_id, IPC_RMID, NULL);
    exit(0);
}

static void spawn(const char* path, const char* arg0, const char* arg1 = nullptr) {
    pid_t p = fork();
    if (p == -1) die_perror("fork");
    if (p == 0) {
        if (arg1) execl(path, arg0, arg1, (char*)NULL);
        else execl(path, arg0, (char*)NULL);
        perror("execl");
        _exit(127);
    }
}

int main() {
    signal(SIGINT, cleanup);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) die_perror("ftok");

    shm_id = shmget(key, 0, 0600);
    if (shm_id != -1) shmctl(shm_id, IPC_RMID, NULL);

    shm_id = shmget(key, sizeof(JaskiniaStan), IPC_CREAT | 0600);
    if (shm_id == -1) die_perror("shmget");

    key = ftok(FTOK_FILE, SEM_ID);
    sem_id = semget(key, 1, IPC_CREAT | 0600);
    if (sem_id == -1) die_perror("semget");

    semun su{};
    su.val = 1;
    if (semctl(sem_id, 0, SETVAL, su) == -1) die_perror("semctl SETVAL");

    key = ftok(FTOK_FILE, MSG_ID);
    msg_id = msgget(key, IPC_CREAT | 0600);
    if (msg_id == -1) die_perror("msgget");

    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) die_perror("shmat");

    lock_sem(sem_id);
    memset(stan, 0, sizeof(JaskiniaStan));
    stan->start_time = time(NULL);
    stan->end_time = stan->start_time + SIM_SECONDS;
    unlock_sem(sem_id);

    if (shmdt(stan) == -1) perror("shmdt");

    spawn("./Kasjer", "Kasjer");
    spawn("./Przewodnik", "Przewodnik", "1");
    spawn("./Przewodnik", "Przewodnik", "2");
    spawn("./Straznik", "Straznik");

    time_t end_time = time(NULL) + SIM_SECONDS;

    while (time(NULL) < end_time) {
        sleep(1);
        spawn("./Zwiedzajacy", "Zwiedzajacy");
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
    }

    cleanup(0);
    return 0;
}
