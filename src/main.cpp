// main.cpp - Główny kontroler symulacji jaskini
// Tworzy zasoby IPC, spawnuje procesy, sprząta przy zakończeniu
#include "common.hpp"
#include <iostream>
#include <sys/wait.h>

using namespace std;

int shm_id = -1, sem_id = -1, msg_id = -1;

static volatile sig_atomic_t g_shutdown = 0;

static pid_t g_cashier = -1;
static pid_t g_guide1 = -1;
static pid_t g_guide2 = -1;
static pid_t g_guard = -1;

static JaskiniaStan* g_stan = nullptr;

static void request_shutdown(int) {
    g_shutdown = 1;
}

// Uruchamia proces potomny (fork + exec)
static pid_t spawn(const char* path, const char* arg0, const char* arg1 = nullptr) {
    pid_t p = fork();
    if (p == -1) die_perror("fork");
    if (p == 0) {
        if (arg1) execl(path, arg0, arg1, (char*)NULL);
        else execl(path, arg0, (char*)NULL);
        perror("execl");
        _exit(127);
    }
    return p;
}

// Parsuje string na int z walidacją
static int parse_int(const char* s) {
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    if (!s || *s == '\0' || !end || *end != '\0') return -1;
    if (v < 0 || v > 1000000) return -1;
    return (int)v;
}

// === CLEANUP - sprzątanie zasobów IPC ===
// Wywoływane przy SIGINT (Ctrl+C) lub SIGTERM (od strażnika)
// Kolejność: 1) ignoruj kolejne sygnały 2) SIGTERM do potomków
//            3) poczekaj, a potem SIGKILL jeśli trzeba 4) waitpid() 5) shmdt 6) usuń IPC
static void cleanup(int) {
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    pid_t kids[4] = { g_guard, g_cashier, g_guide1, g_guide2 };

    for (pid_t p : kids) {
        if (p > 0) kill(p, SIGTERM);
    }

    for (int t = 0; t < 30; t++) {
        int alive = 0;
        for (pid_t p : kids) {
            if (p <= 0) continue;
            if (kill(p, 0) == 0) alive++;
        }
        if (alive == 0) break;
        usleep(100000);
    }

    for (pid_t p : kids) {
        if (p <= 0) continue;
        if (kill(p, 0) == 0) kill(p, SIGKILL);
    }

    while (waitpid(-1, NULL, 0) > 0) {}

    if (g_stan && g_stan != (void*)-1) {
        shmdt(g_stan);
        g_stan = nullptr;
    }

    // === PODSUMOWANIE przed usunięciem zasobów IPC ===
    if (shm_id != -1) {
        JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, SHM_RDONLY);
        if (stan != (void*)-1) {
            int total_t1 = stan->bilety_total_t1;
            int total_t2 = stan->bilety_total_t2;
            int przychod = stan->przychod;
            int darmowe = stan->bilety_darmowe;
            int znizka = stan->bilety_znizka;

            shmdt(stan);

            cout << "\n========== PODSUMOWANIE ==========" << endl;
            cout << "Bilety T1:        " << total_t1 << endl;
            cout << "Bilety T2:        " << total_t2 << endl;
            cout << "Razem biletow:    " << (total_t1 + total_t2) << endl;
            cout << "  - darmowych:    " << darmowe << " (dzieci <3 lat)" << endl;
            cout << "  - ze znizka:    " << znizka << " (powrot -50%)" << endl;
            cout << "PRZYCHOD:         " << przychod << " zl" << endl;
            cout << "==================================\n" << endl;

            char summary[256];
            snprintf(summary, sizeof(summary), "Bilety: %d, Przychod: %d zl", total_t1 + total_t2, przychod);
            logf_simple("MAIN", summary);
        }
    }

    logf_simple("MAIN", "STOP");

    if (msg_id != -1 && msgctl(msg_id, IPC_RMID, NULL) == -1) perror("msgctl");
    if (shm_id != -1 && shmctl(shm_id, IPC_RMID, NULL) == -1) perror("shmctl");
    if (sem_id != -1 && semctl(sem_id, 0, IPC_RMID) == -1) perror("semctl");

    if (unlink(FTOK_FILE) == -1 && errno != ENOENT) perror("unlink");
    _exit(0);
}

int main(int argc, char** argv) {
    signal(SIGINT, request_shutdown);
    signal(SIGTERM, request_shutdown);
    signal(SIGUSR1, request_shutdown);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGTSTP, SIG_DFL);

    int opening_hour = OPENING_HOUR;
    int closing_hour = CLOSING_HOUR;
    int spawn_ms = SPAWN_MS_DEFAULT;

    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--open" && i + 1 < argc) {
            int v = parse_int(argv[++i]);
            if (v < 0 || v > 23) {
                cerr << "Niepoprawny --open (0..23)\n";
                return 1;
            }
            opening_hour = v;
        } else if (a == "--close" && i + 1 < argc) {
            int v = parse_int(argv[++i]);
            if (v < 1 || v > 24) {
                cerr << "Niepoprawny --close (1..24)\n";
                return 1;
            }
            closing_hour = v;
        } else if (a == "--spawn-ms" && i + 1 < argc) {
            int v = parse_int(argv[++i]);
            if (v < 5 || v > 5000) {
                cerr << "Niepoprawny --spawn-ms (5..5000)\n";
                return 1;
            }
            spawn_ms = v;
        } else {
            cerr << "Uzycie: ./SymulacjaJaskini [--open H] [--close H] [--spawn-ms MS]\n";
            return 1;
        }
    }

    if (closing_hour <= opening_hour) {
        cerr << "Blad: --close musi byc wieksze niz --open\n";
        return 1;
    }

    int sim_seconds = (closing_hour - opening_hour) * SECONDS_PER_HOUR;

    unlink(LOG_FILE);
    unlink("kladka_t1.log");
    unlink("kladka_t2.log");

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
    g_stan = stan;

    lock_sem(sem_id);
    memset(stan, 0, sizeof(JaskiniaStan));
    q_init(stan->q_t1);
    q_init(stan->q_t1_prio);
    q_init(stan->q_t2);
    q_init(stan->q_t2_prio);
    stan->start_time = time(NULL);
    stan->end_time = stan->start_time + sim_seconds;
    stan->sim_opening_hour = opening_hour;
    stan->sim_closing_hour = closing_hour;
    stan->active_visitors = 0;
    stan->przychod = 0;
    stan->bilety_darmowe = 0;
    stan->bilety_znizka = 0;
    stan->bilety_total_t1 = 0;
    stan->bilety_total_t2 = 0;
    stan->user_shutdown_request = 0;
    unlock_sem(sem_id);

    cout << COL_CYAN << "[MAIN]" << COL_RESET << " Jaskinia otwarta: " << opening_hour << ":00 - " << closing_hour << ":00" << endl;
    cout << COL_CYAN << "[MAIN]" << COL_RESET << " Czas symulacji: " << sim_seconds << "s (" << SECONDS_PER_HOUR << "s = 1h)" << endl;
    cout << COL_CYAN << "[MAIN]" << COL_RESET << " Aby zakonczyc Ctrl+C" << endl;

    g_cashier = spawn("./Kasjer", "Kasjer");
    g_guide1 = spawn("./Przewodnik", "Przewodnik", "1");
    g_guide2 = spawn("./Przewodnik", "Przewodnik", "2");
    g_guard = spawn("./Straznik", "Straznik");

    while (!g_shutdown) {
        usleep((useconds_t)spawn_ms * 1000);
        if (g_shutdown) break;
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            lock_sem(sem_id);
            if (stan->active_visitors > 0) stan->active_visitors--;
            unlock_sem(sem_id);
        }

        lock_sem(sem_id);
        int current_visitors = stan->active_visitors;
        unlock_sem(sem_id);

        if (current_visitors >= MAX_VISITORS) {
            continue;
        }

        spawn("./Zwiedzajacy", "Zwiedzajacy");
        lock_sem(sem_id);
        stan->active_visitors++;
        unlock_sem(sem_id);
    }

    cleanup(0);
    return 0;
}
