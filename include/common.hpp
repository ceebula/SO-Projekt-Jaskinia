#pragma once

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

#include <unistd.h>
#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <ctime>

#include <fcntl.h>

static inline void die_perror(const char* ctx) {
    perror(ctx);
    exit(EXIT_FAILURE);
}

static constexpr const char* FTOK_FILE = "ftok.key";
static constexpr const char* LOG_FILE  = "symulacja.log";

static constexpr int SHM_ID = 'S';
static constexpr int SEM_ID = 'M';
static constexpr int MSG_ID = 'Q';

static constexpr int N1 = 10;
static constexpr int N2 = 10;
static constexpr int K  = 3;

static constexpr int ALARM_SECONDS = 10;

static constexpr int SIM_SECONDS_DEFAULT = 60;
static constexpr int SPAWN_MS_DEFAULT = 1000;

static constexpr int T1_MS = 2000;
static constexpr int T2_MS = 3000;

static constexpr int QCAP = 128;

static constexpr long MSG_KASJER = 1;
static constexpr long MSG_ENTER_BASE = 1000000;
static constexpr long MSG_EXIT_T1 = 2000001;
static constexpr long MSG_EXIT_T2 = 2000002;

static inline void log_append(const char* line) {
    int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd == -1) { perror("open log"); return; }
    ssize_t n = write(fd, line, (size_t)strlen(line));
    if (n == -1) perror("write log");
    if (close(fd) == -1) perror("close log");
}

static inline void logf_simple(const char* tag, const char* msg) {
    char buf[512];
    int len = snprintf(buf, sizeof(buf), "[%s %d] %s\n", tag, (int)getpid(), msg);
    if (len > 0) log_append(buf);
}

enum KierunekRuchu {
    DIR_NONE = 0,
    DIR_ENTERING = 1,
    DIR_LEAVING = 2
};

struct GroupItem {
    int group_size;
    pid_t pids[2];
};

struct GroupQueue {
    int head;
    int tail;
    int count;
    GroupItem items[QCAP];
};

static inline void q_init(GroupQueue& q) {
    q.head = 0;
    q.tail = 0;
    q.count = 0;
}

static inline int q_push(GroupQueue& q, const GroupItem& it) {
    if (q.count >= QCAP) return -1;
    q.items[q.tail] = it;
    q.tail = (q.tail + 1) % QCAP;
    q.count++;
    return 0;
}

static inline int q_pop(GroupQueue& q, GroupItem& out) {
    if (q.count <= 0) return -1;
    out = q.items[q.head];
    q.head = (q.head + 1) % QCAP;
    q.count--;
    return 0;
}

struct JaskiniaStan {
    int bilety_sprzedane_t1;
    int bilety_sprzedane_t2;

    int oczekujacy_t1;
    int oczekujacy_t2;

    GroupQueue q_t1;
    GroupQueue q_t1_prio;
    GroupQueue q_t2;
    GroupQueue q_t2_prio;

    int osoby_trasa1;
    int osoby_trasa2;

    int osoby_na_kladce;
    int kierunek_ruchu_kladka;

    int alarm_t1;
    int alarm_t2;
    time_t alarm_do_t1;
    time_t alarm_do_t2;

    time_t start_time;
    time_t end_time;

    pid_t przewodnik_t1_pid;
    pid_t przewodnik_t2_pid;
};

struct Wiadomosc {
    long mtype;
    int id_nadawcy;
    int typ_biletu;
    int odpowiedz;
    int wiek;
    int powrot;
    int group_size;
    pid_t pids[2];
};

struct MsgEnter {
    long mtype;
    int trasa;
};

struct MsgExit {
    long mtype;
    pid_t pid;
    int group_size;
};

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
#if defined(__linux__)
    struct seminfo* __buf;
#endif
};

static inline void lock_sem(int semid) {
    sembuf op{};
    op.sem_num = 0;
    op.sem_op = -1;
    op.sem_flg = 0;
    if (semop(semid, &op, 1) == -1) die_perror("semop lock");
}

static inline void unlock_sem(int semid) {
    sembuf op{};
    op.sem_num = 0;
    op.sem_op = 1;
    op.sem_flg = 0;
    if (semop(semid, &op, 1) == -1) die_perror("semop unlock");
}
