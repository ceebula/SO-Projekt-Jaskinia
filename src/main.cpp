#include "common.hpp"
#include <iostream>
#include <cstring>
#include <sys/wait.h>

using namespace std;

int shm_id = -1;
int sem_id = -1;

void cleanup(int) {
    kill(0, SIGTERM);
    if (shm_id != -1) shmctl(shm_id, IPC_RMID, NULL);
    if (sem_id != -1) semctl(sem_id, 0, IPC_RMID);
    exit(0);
}

int main() {
    signal(SIGINT, cleanup);

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) { perror("Błąd ftok"); return 1; }

    shm_id = shmget(key, sizeof(JaskiniaStan), IPC_CREAT | 0666);
    if (shm_id == -1) { perror("Błąd shmget"); return 1; }

    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) { perror("Błąd shmat"); cleanup(0); }

    memset(stan, 0, sizeof(JaskiniaStan));
    shmdt(stan);

    key = ftok(FTOK_FILE, SEM_ID);
    sem_id = semget(key, 1, IPC_CREAT | 0666);
    if (sem_id == -1) { perror("semget"); cleanup(0); }
    semctl(sem_id, 0, SETVAL, 1);

    if (fork() == 0) { execl("./Kasjer", "Kasjer", NULL); exit(1); }
    if (fork() == 0) { execl("./Przewodnik", "Przewodnik", NULL); exit(1); }

    while (true) {
        sleep(2);
        if (fork() == 0) { execl("./Zwiedzajacy", "Zwiedzajacy", NULL); exit(1); }
        while (waitpid(-1, NULL, WNOHANG) > 0);
    }
}