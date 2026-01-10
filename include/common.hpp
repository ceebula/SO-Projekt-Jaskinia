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
#include <errno.h>

// Stałe do kluczy IPC
#define FTOK_FILE "CMakeLists.txt"
#define SHM_ID 1
#define SEM_ID 2

// Stałe projektowe (Limity)
#define N1 10      // Pojemność trasy 1
#define N2 15      // Pojemność trasy 2
#define K  3       // Pojemność kładki

// Definicje kierunków ruchu na kładce
#define DIR_NONE 0
#define DIR_ENTERING 1 // Wchodzenie
#define DIR_LEAVING 2  // Wychodzenie

// Struktura w pamięci dzielonej
struct JaskiniaStan {
    // Liczniki osób w strefach
    int osoby_na_kladce;
    int osoby_trasa1;
    int osoby_trasa2;
    
    // Sterowanie ruchem (dla Przewodników)
    int kierunek_ruchu_kladka; // DIR_NONE, DIR_ENTERING, DIR_LEAVING
    
    // Licznik biletów (dla Kasjera)
    int bilety_sprzedane_t1;
    int bilety_sprzedane_t2;
};

#endif