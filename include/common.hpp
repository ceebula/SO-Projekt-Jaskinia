#ifndef COMMON_HPP
#define COMMON_HPP

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define FTOK_FILE "CMakeLists.txt"
#define SHM_ID 1
#define SEM_ID 2
#define N1 10
#define N2 15
#define K  3

#define DIR_NONE 0
#define DIR_ENTERING 1
#define DIR_LEAVING 2

struct JaskiniaStan {
    int osoby_na_kladce;
    int osoby_trasa1;
    int osoby_trasa2;
    int kierunek_ruchu_kladka;
    int bilety_sprzedane_t1;
    int bilety_sprzedane_t2;
};

inline void lock_sem(int sem_id) {
    struct sembuf sb = {0, -1, 0};
    if (semop(sem_id, &sb, 1) == -1) exit(1);
}

inline void unlock_sem(int sem_id) {
    struct sembuf sb = {0, 1, 0};
    if (semop(sem_id, &sb, 1) == -1) exit(1);
}

#endif