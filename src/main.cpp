#include "common.hpp"
#include <iostream>
#include <sys/wait.h>

using namespace std;

int shm_id = -1, sem_id = -1, msg_id = -1;

void cleanup(int) {
    kill(0, SIGTERM);
    if (shm_id != -1 && shmctl(shm_id, IPC_RMID, NULL) == -1) perror("shmctl");
    if (sem_id != -1 && semctl(sem_id, 0, IPC_RMID) == -1) perror("semctl");
    if (msg_id != -1 && msgctl(msg_id, IPC_RMID, NULL) == -1) perror("msgctl");
    exit(0);
}

int main() {
    signal(SIGINT, cleanup);

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) die_perror("ftok");

    shm_id = shmget(key, sizeof(JaskiniaStan), IPC_CREAT | 0600);
    if (shm_id == -1) die_perror("shmget");

    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) die_perror("shmat");
    memset(stan, 0, sizeof(JaskiniaStan));
    shmdt(stan);

    key = ftok(FTOK_FILE, SEM_ID);
    sem_id = semget(key, 1, IPC_CREAT | 0600);
    if (sem_id == -1) die_perror("semget");

    semun su{};
    su.val = 1;
    semctl(sem_id, 0, SETVAL, su);

    key = ftok(FTOK_FILE, MSG_ID);
    msg_id = msgget(key, IPC_CREAT | 0600);
    if (msg_id == -1) die_perror("msgget");

    if (fork() == 0) execl("./Kasjer", "Kasjer", NULL);
    if (fork() == 0) execl("./Przewodnik", "Przewodnik", NULL);
    if (fork() == 0) execl("./Straznik", "Straznik", NULL);

    while (true) {
        sleep(2);
        if (fork() == 0) execl("./Zwiedzajacy", "Zwiedzajacy", NULL);
        while (waitpid(-1, NULL, WNOHANG) > 0);
    }
}
