// common.hpp - Wspólne definicje, stałe i funkcje pomocnicze dla symulacji jaskini
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

// Kolory ANSI dla wyjścia terminala
#define COL_RESET   "\033[0m"
#define COL_RED     "\033[31m"
#define COL_GREEN   "\033[32m"
#define COL_YELLOW  "\033[33m"
#define COL_BLUE    "\033[34m"
#define COL_MAGENTA "\033[35m"
#define COL_CYAN    "\033[36m"

static inline void die_perror(const char* ctx) {
    perror(ctx);
    exit(EXIT_FAILURE);
}

// === Pliki IPC ===
static constexpr const char* FTOK_FILE = "ftok.key";  ///< Plik klucza dla ftok()
static constexpr const char* LOG_FILE  = "symulacja.log";  ///< Plik logu symulacji

// === Identyfikatory IPC (dla ftok) ===
static constexpr int SHM_ID = 'S';  ///< ID pamięci dzielonej
static constexpr int SEM_ID = 'M';  ///< ID semafora
static constexpr int MSG_ID = 'Q';  ///< ID kolejki komunikatów

// === Godziny pracy jaskini ===
static constexpr int OPENING_HOUR = 8;      ///< Domyślna godzina otwarcia (Tp)
static constexpr int CLOSING_HOUR = 18;     ///< Domyślna godzina zamknięcia (Tk)
static constexpr int SECONDS_PER_HOUR = 6;  ///< Sekund rzeczywistych na godzinę symulacji

// === Pojemności tras i kładki ===
static constexpr int N1 = 10;  ///< Max osób na trasie T1
static constexpr int N2 = 10;  ///< Max osób na trasie T2
static constexpr int K  = 3;   ///< Max osób na kładce jednocześnie

// === Czasy zwiedzania ===
static constexpr int T1_MS = 2000;           ///< Czas zwiedzania trasy T1 [ms]
static constexpr int T2_MS = 3000;           ///< Czas zwiedzania trasy T2 [ms]
static constexpr int BRIDGE_DURATION_MS = 300;  ///< Czas przejścia przez kładkę [ms]

// === Parametry symulacji ===
static constexpr int SPAWN_MS_DEFAULT = 1000;  ///< Domyślny interwał spawnu turystów [ms]
static constexpr int MAX_VISITORS = 50;        ///< Max aktywnych procesów turystów
static constexpr int TICKET_PRICE = 20;        ///< Cena biletu w zł

static constexpr int QCAP = 128;  ///< Pojemność kolejki grup

// === Typy komunikatów w kolejce ===
static constexpr long MSG_KASJER = 1;       ///< Komunikat do kasjera (zakup biletu)
static constexpr long MSG_ENTER_BASE = 1000000;  ///< Baza mtype dla powiadomień o wejściu
static constexpr long MSG_EXIT_T1 = 2000001;     ///< Komunikat wyjścia z trasy T1
static constexpr long MSG_EXIT_T2 = 2000002;     ///< Komunikat wyjścia z trasy T2

// Dopisuje linię do pliku logu (używa open/write/close)
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

// Kierunek ruchu na kładce (wąska - ruch tylko w jedną stronę)
enum KierunekRuchu {
    DIR_NONE = 0,      // kładka wolna
    DIR_ENTERING = 1,  // ruch do jaskini
    DIR_LEAVING = 2    // ruch z jaskini
};

// Element kolejki - grupa zwiedzających
struct GroupItem {
    int group_size;   // rozmiar grupy (1 lub 2)
    pid_t pids[2];    // PID-y członków
};

// Kolejka cykliczna grup oczekujących (w pamięci dzielonej)
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

// Stan jaskini w pamięci dzielonej (dostęp chroniony semaforem)
struct JaskiniaStan {
    // Liczniki biletów
    int bilety_sprzedane_t1;  // aktywne bilety T1
    int bilety_sprzedane_t2;
    int bilety_total_t1;      // łączna sprzedaż
    int bilety_total_t2;

    // Kolejki oczekujących
    int oczekujacy_t1;
    int oczekujacy_t2;
    GroupQueue q_t1;          // kolejka zwykła T1
    GroupQueue q_t1_prio;     // kolejka priorytetowa T1 (powroty)
    GroupQueue q_t2;
    GroupQueue q_t2_prio;

    // Stan tras
    int osoby_trasa1;
    int osoby_trasa2;

    // Stan kładek (osobna dla każdej trasy)
    int osoby_na_kladce[2];       // [0]=T1, [1]=T2
    int kierunek_ruchu_kladka[2]; // [0]=T1, [1]=T2

    // Flagi alarmowe (od strażnika)
    int alarm_t1;
    int alarm_t2;
    
    // Flaga ręcznego zakończenia przez użytkownika
    int user_shutdown_request;

    // Parametry czasowe
    time_t start_time;
    time_t end_time;
    int sim_opening_hour;
    int sim_closing_hour;

    // Statystyki
    int active_visitors;
    int przychod;
    int bilety_darmowe;
    int bilety_znizka;

    // PID-y przewodników (do sygnałów)
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

// Blokada semafora z możliwością przerwania sygnałem (zwraca -1 przy EINTR)
static inline int lock_sem_interruptible(int semid) {
    sembuf op{};
    op.sem_num = 0;
    op.sem_op = -1;  // operacja P (wait)
    op.sem_flg = 0;
    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) return -1;
        die_perror("semop lock");
    }
    return 0;
}

// Blokada semafora (mutex) - operacja P (wait)
static inline void lock_sem(int semid) {
    sembuf op{};
    op.sem_num = 0;
    op.sem_op = -1;  // operacja P
    op.sem_flg = 0;
    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) continue;
        die_perror("semop lock");
    }
}

// Odblokowanie semafora (mutex) - operacja V (signal)
static inline void unlock_sem(int semid) {
    sembuf op{};
    op.sem_num = 0;
    op.sem_op = 1;  // operacja V
    op.sem_flg = 0;
    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) continue;
        die_perror("semop unlock");
    }
}
