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

static inline void die_perror(const char* ctx) {
    perror(ctx);
    exit(EXIT_FAILURE);
}

static constexpr const char* FTOK_FILE = ".";
static constexpr int SHM_ID = 'S';
static constexpr int SEM_ID = 'M';
static constexpr int MSG_ID = 'Q';

static constexpr int N1 = 10;
static constexpr int N2 = 10;
static constexpr int K  = 3;

static constexpr int ALARM_SECONDS = 10;
static constexpr int SIM_SECONDS = 60;

enum KierunekRuchu {
    DIR_NONE = 0,
    DIR_ENTERING = 1,
    DIR_LEAVING = 2
};

struct JaskiniaStan {
    int bilety_sprzedane_t1;
    int bilety_sprzedane_t2;

    int oczekujacy_t1;
    int oczekujacy_t2;

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
};

struct Wiadomosc {
    long mtype;
    int id_nadawcy;
    int typ_biletu;
    int odpowiedz;
    int wiek;
    int powrot;
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
