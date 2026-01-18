#include "common.hpp"
#include <iostream>
#include <cstring>
#include <sys/wait.h>

using namespace std;

int shm_id = -1, sem_id = -1, msg_id = -1;

void cleanup(int) {
    if (kill(0, SIGTERM) == -1 && errno != ESRCH) perror("kill");
    if (shm_id != -1 && shmctl(shm_id, IPC_RMID, NULL) == -1) perror("shmctl IPC_RMID");
    if (sem_id != -1 && semctl(sem_id, 0, IPC_RMID) == -1) perror("semctl IPC_RMID");
    if (msg_id != -1 && msgctl(msg_id, IPC_RMID, NULL) == -1) perror("msgctl IPC_RMID");
    exit(0);
}

int main() {
    signal(SIGINT, cleanup);

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) { perror("Błąd ftok"); return 1; }

    shm_id = shmget(key, sizeof(JaskiniaStan), IPC_CREAT | 0600);
    if (shm_id == -1) { perror("Błąd shmget"); return 1; }

    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) { perror("Błąd shmat"); cleanup(0); }

    memset(stan, 0, sizeof(JaskiniaStan));
    if (shmdt(stan) == -1) perror("Błąd shmdt");

    key = ftok(FTOK_FILE, SEM_ID);
    sem_id = semget(key, 1, IPC_CREAT | 0600);
    if (sem_id == -1) { perror("semget"); cleanup(0); }
    semun su{};
    su.val = 1;
    if (semctl(sem_id, 0, SETVAL, su) == -1) { perror("semctl SETVAL"); cleanup(0); }

    key = ftok(FTOK_FILE, MSG_ID);
    msg_id = msgget(key, IPC_CREAT | 0600);
    if (msg_id == -1) { perror("msgget"); cleanup(0); }

    if (fork() == 0) { execl("./Kasjer", "Kasjer", NULL); perror("execl Kasjer"); _exit(127); }
    if (fork() == 0) { execl("./Przewodnik", "Przewodnik", NULL); perror("execl Przewodnik"); _exit(127); }

    while (true) {
        sleep(2);
        if (fork() == 0) { execl("./Zwiedzajacy", "Zwiedzajacy", NULL); perror("execl Zwiedzajacy"); _exit(127); }
        while (waitpid(-1, NULL, WNOHANG) > 0);
    }
}
