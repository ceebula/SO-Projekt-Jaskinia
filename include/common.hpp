#ifndef COMMON_HPP
#define COMMON_HPP

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define FTOK_FILE "common.hpp"
#define SHM_ID 'M'
#define SEM_ID 'S'

struct JaskiniaStan {
    int osoby_na_kladce_wejscie;
    int osoby_na_kladce_wyjscie;
    int osoby_trasa1;
    int osoby_trasa2;
    int liczba_wolnych_biletow_trasa1;
    int liczba_wolnych_biletow_trasa2;
};

#endif