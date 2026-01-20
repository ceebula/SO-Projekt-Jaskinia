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

    unlink(FTOK_FILE);
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

static int parse_int(const char* s) {
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    if (!s || *s == '\0' || !end || *end != '\0') return -1;
    if (v < 0 || v > 1000000) return -1;
    return (int)v;
}

int main(int argc, char** argv) {
    signal(SIGINT, cleanup);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    int sim_seconds = SIM_SECONDS_DEFAULT;
    int spawn_ms = SPAWN_MS_DEFAULT;

    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--time" && i + 1 < argc) {
            int v = parse_int(argv[++i]);
            if (v < 5 || v > 600) {
                cerr << "Niepoprawny --time (5..600)\n";
                return 1;
            }
            sim_seconds = v;
        } else if (a == "--spawn-ms" && i + 1 < argc) {
            int v = parse_int(argv[++i]);
            if (v < 100 || v > 5000) {
                cerr << "Niepoprawny --spawn-ms (100..5000)\n";
                return 1;
            }
            spawn_ms = v;
        } else {
            cerr << "Uzycie: ./SymulacjaJaskini [--time SEK] [--spawn-ms MS]\n";
            return 1;
        }
    }

    int fd = creat(FTOK_FILE, 0600);
    if (fd == -1) die_perror("creat ftok.key");
    if (close(fd) == -1) perror("close ftok.key");

    logf_simple("MAIN", "START");

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
    q_init(stan->q_t1);
    q_init(stan->q_t1_prio);
    q_init(stan->q_t2);
    q_init(stan->q_t2_prio);
    stan->start_time = time(NULL);
    stan->end_time = stan->start_time + sim_seconds;
    unlock_sem(sem_id);

    if (shmdt(stan) == -1) perror("shmdt");

    spawn("./Kasjer", "Kasjer");
    spawn("./Przewodnik", "Przewodnik", "1");
    spawn("./Przewodnik", "Przewodnik", "2");
    spawn("./Straznik", "Straznik");

    time_t end_time = time(NULL) + sim_seconds;

    while (time(NULL) < end_time) {
        usleep((useconds_t)spawn_ms * 1000);
        spawn("./Zwiedzajacy", "Zwiedzajacy");
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
    }

    logf_simple("MAIN", "STOP");
    cleanup(0);
    return 0;
}
