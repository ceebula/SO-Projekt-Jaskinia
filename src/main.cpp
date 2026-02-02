// main.cpp - Główny kontroler symulacji jaskini
// Tworzy zasoby IPC, spawnuje procesy, sprząta przy zakończeniu
#include "common.hpp"
#include <iostream>
#include <sys/wait.h>

using namespace std;

int shm_id = -1, sem_id = -1, msg_id = -1;

// Handler SIGINT/SIGTERM - sprzątanie zasobów IPC i zakończenie procesów
static void cleanup(int) {
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    kill(0, SIGTERM);  // SIGTERM do całej grupy

    for (int i = 0; i < 50; i++) {
        int rc = waitpid(-1, NULL, WNOHANG);
        if (rc == -1 && errno == ECHILD) break;
        usleep(100000);
    }

    if (shm_id != -1 && shmctl(shm_id, IPC_RMID, NULL) == -1) perror("shmctl");
    if (sem_id != -1 && semctl(sem_id, 0, IPC_RMID) == -1) perror("semctl");
    if (msg_id != -1 && msgctl(msg_id, IPC_RMID, NULL) == -1) perror("msgctl");

    if (unlink(FTOK_FILE) == -1 && errno != ENOENT) perror("unlink");
    exit(0);
}

// Uruchamia proces potomny (fork + exec)
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

// Parsuje string na int z walidacją
static int parse_int(const char* s) {
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    if (!s || *s == '\0' || !end || *end != '\0') return -1;
    if (v < 0 || v > 1000000) return -1;
    return (int)v;
}

// Handler SIGTSTP (Ctrl+Z) - pauza symulacji
static void handle_tstp(int) {
    kill(0, SIGSTOP);
}

int main(int argc, char** argv) {
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);  // obsługa timeout
    signal(SIGTSTP, handle_tstp);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

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

    lock_sem(sem_id);
    memset(stan, 0, sizeof(JaskiniaStan));
    q_init(stan->q_t1);
    q_init(stan->q_t1_prio);
    q_init(stan->q_t2);
    q_init(stan->q_t2_prio);
    stan->start_time = time(NULL);
    stan->end_time = stan->start_time + sim_seconds;  // Guard używa do obliczenia czasu wysłania sygnału
    stan->sim_opening_hour = opening_hour;
    stan->sim_closing_hour = closing_hour;
    stan->active_visitors = 0;
    stan->przychod = 0;
    stan->bilety_darmowe = 0;
    stan->bilety_znizka = 0;
    stan->bilety_total_t1 = 0;
    stan->bilety_total_t2 = 0;
    unlock_sem(sem_id);

    cout << COL_CYAN << "[MAIN]" << COL_RESET << " Jaskinia otwarta: " << opening_hour << ":00 - " << closing_hour << ":00" << endl;
    cout << COL_CYAN << "[MAIN]" << COL_RESET << " Czas symulacji: " << sim_seconds << "s (" << SECONDS_PER_HOUR << "s = 1h)" << endl;

    spawn("./Kasjer", "Kasjer");
    spawn("./Przewodnik", "Przewodnik", "1");
    spawn("./Przewodnik", "Przewodnik", "2");
    spawn("./Straznik", "Straznik");

    // Main działa w pętli dopóki nie zostanie zakończony przez cleanup (SIGTERM/SIGINT)
    // Guard kontroluje zakończenie symulacji wysyłając sygnały
    while (true) {
        usleep((useconds_t)spawn_ms * 1000);

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


    logf_simple("MAIN", "Czas symulacji uplynal, czekam na procesy...");

    time_t grace_end = time(NULL) + 10;
    while (time(NULL) < grace_end) {
        int rc = waitpid(-1, NULL, WNOHANG);
        if (rc == -1 && errno == ECHILD) break;
        usleep(100000);
    }

    lock_sem(sem_id);
    int total_t1 = stan->bilety_total_t1;
    int total_t2 = stan->bilety_total_t2;
    int przychod = stan->przychod;
    int darmowe = stan->bilety_darmowe;
    int znizka = stan->bilety_znizka;
    unlock_sem(sem_id);

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

    logf_simple("MAIN", "STOP");
    cleanup(0);
    return 0;
}
