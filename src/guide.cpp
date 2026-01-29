// guide.cpp - Proces przewodnika (zarządzanie trasą, synchronizacja kładki)
// Obsługuje sygnały SIGUSR1/SIGUSR2 od strażnika
#include "common.hpp"
#include <iostream>
#include <cstdlib>

using namespace std;

static JaskiniaStan* stan = nullptr;
static int trasa = 0;
static int sem_id = -1;
static int msg_id = -1;
static volatile sig_atomic_t g_terminated = 0;

static void handle_term(int) {
    g_terminated = 1;
}

static void alarm_t1(int) {
    if (!stan) return;
    lock_sem(sem_id);
    stan->alarm_t1 = 1;
    unlock_sem(sem_id);
    cout << COL_GREEN << "[PRZEWODNIK T1]" << COL_RESET << " Alarm! Blokada nowych wejsc do zamkniecia" << endl;
    logf_simple("PRZEWODNIK", "Alarm T1 - blokada do zamkniecia");
}

static void alarm_t2(int) {
    if (!stan) return;
    lock_sem(sem_id);
    stan->alarm_t2 = 1;
    unlock_sem(sem_id);
    cout << COL_GREEN << "[PRZEWODNIK T2]" << COL_RESET << " Alarm! Blokada nowych wejsc do zamkniecia" << endl;
    logf_simple("PRZEWODNIK", "Alarm T2 - blokada do zamkniecia");
}

static bool alarm_aktywny_locked() {
    if (trasa == 1) {
        return stan->alarm_t1 != 0;
    } else {
        return stan->alarm_t2 != 0;
    }
}

static void notify_cancel(pid_t pid) {
    if (pid <= 0) return;
    MsgEnter msgCancel{};
    msgCancel.mtype = MSG_ENTER_BASE + pid;
    msgCancel.trasa = -1;
    if (msgsnd(msg_id, &msgCancel, sizeof(msgCancel) - sizeof(long), IPC_NOWAIT) == -1) {
        if (errno != EAGAIN) perror("msgsnd cancel");
    }
}

static int cancel_waiting_groups_locked() {
    int sum = 0;
    GroupItem it{};
    if (trasa == 1) {
        while (q_pop(stan->q_t1_prio, it) == 0) {
            int gs = (it.group_size > 0) ? it.group_size : 1;
            sum += gs;
            for (int i = 0; i < gs && i < 2; i++) notify_cancel(it.pids[i]);
        }
        while (q_pop(stan->q_t1, it) == 0) {
            int gs = (it.group_size > 0) ? it.group_size : 1;
            sum += gs;
            for (int i = 0; i < gs && i < 2; i++) notify_cancel(it.pids[i]);
        }
        stan->oczekujacy_t1 = 0;
        stan->bilety_sprzedane_t1 -= sum;
        if (stan->bilety_sprzedane_t1 < 0) stan->bilety_sprzedane_t1 = 0;
    } else {
        while (q_pop(stan->q_t2_prio, it) == 0) {
            int gs = (it.group_size > 0) ? it.group_size : 1;
            sum += gs;
            for (int i = 0; i < gs && i < 2; i++) notify_cancel(it.pids[i]);
        }
        while (q_pop(stan->q_t2, it) == 0) {
            int gs = (it.group_size > 0) ? it.group_size : 1;
            sum += gs;
            for (int i = 0; i < gs && i < 2; i++) notify_cancel(it.pids[i]);
        }
        stan->oczekujacy_t2 = 0;
        stan->bilety_sprzedane_t2 -= sum;
        if (stan->bilety_sprzedane_t2 < 0) stan->bilety_sprzedane_t2 = 0;
    }
    return sum;
}

static int dequeue_group_locked(GroupItem& out, bool& from_prio) {
    from_prio = false;
    if (trasa == 1) {
        if (stan->q_t1_prio.count > 0) { from_prio = true; return q_pop(stan->q_t1_prio, out); }
        if (stan->q_t1.count > 0) return q_pop(stan->q_t1, out);
        return -1;
    } else {
        if (stan->q_t2_prio.count > 0) { from_prio = true; return q_pop(stan->q_t2_prio, out); }
        if (stan->q_t2.count > 0) return q_pop(stan->q_t2, out);
        return -1;
    }
}

static int waiting_count_locked() {
    if (trasa == 1) return stan->q_t1.count + stan->q_t1_prio.count;
    return stan->q_t2.count + stan->q_t2_prio.count;
}

static const char* dir_to_str(int kierunek) {
    return (kierunek == DIR_ENTERING) ? "IN" : 
           (kierunek == DIR_LEAVING) ? "OUT" : "NONE";
}

static void log_kladka(const char* action, int kladka_przed, int kier_przed, 
                       int kladka_po, int kier_po) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    
    // Czas od startu symulacji
    long elapsed_ms = (now.tv_sec - stan->start_time) * 1000 + now.tv_nsec / 1000000;
    if (elapsed_ms < 0) elapsed_ms = 0;
    
    int fd = open("kladka.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd == -1) return;
    
    struct flock fl = {F_WRLCK, SEEK_SET, 0, 0, 0};
    fcntl(fd, F_SETLKW, &fl);
    
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "%3ld.%03lds [T%d] %-18s kladka=%d->%d kierunek=%s->%s\n", 
                       elapsed_ms / 1000, elapsed_ms % 1000,
                       trasa, action, 
                       kladka_przed, kladka_po,
                       dir_to_str(kier_przed), dir_to_str(kier_po));
    if (len > 0) write(fd, buf, (size_t)len);
    
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    close(fd);
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    trasa = atoi(argv[1]);
    if (trasa != 1 && trasa != 2) return 1;

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    signal(SIGTERM, handle_term);
    if (trasa == 1) { signal(SIGUSR1, alarm_t1); signal(SIGUSR2, SIG_IGN); }
    else { signal(SIGUSR2, alarm_t2); signal(SIGUSR1, SIG_IGN); }

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) die_perror("ftok SHM");
    int shm_id = shmget(key, sizeof(JaskiniaStan), 0600);
    if (shm_id == -1) die_perror("shmget");
    stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) die_perror("shmat");

    key = ftok(FTOK_FILE, SEM_ID);
    if (key == -1) die_perror("ftok SEM");
    sem_id = semget(key, 1, 0600);
    if (sem_id == -1) die_perror("semget");

    key = ftok(FTOK_FILE, MSG_ID);
    if (key == -1) die_perror("ftok MSG");
    msg_id = msgget(key, 0600);
    if (msg_id == -1) die_perror("msgget");

    lock_sem(sem_id);
    if (trasa == 1) stan->przewodnik_t1_pid = getpid();
    else stan->przewodnik_t2_pid = getpid();
    unlock_sem(sem_id);

    cout << COL_GREEN << (trasa == 1 ? "[PRZEWODNIK T1]" : "[PRZEWODNIK T2]") << COL_RESET << " Gotowy" << endl;

    long exit_mtype = (trasa == 1) ? MSG_EXIT_T1 : MSG_EXIT_T2;

    while (!g_terminated) {
        time_t now = time(NULL);

        lock_sem(sem_id);
        bool koniec = (stan->end_time != 0 && now >= stan->end_time);
        bool alarm = alarm_aktywny_locked();

        int* wjask = (trasa == 1) ? &stan->osoby_trasa1 : &stan->osoby_trasa2;
        int* bilety = (trasa == 1) ? &stan->bilety_sprzedane_t1 : &stan->bilety_sprzedane_t2;

        int ocz = waiting_count_locked();
        if (trasa == 1) stan->oczekujacy_t1 = ocz;
        else stan->oczekujacy_t2 = ocz;

        if (alarm && *wjask == 0 && stan->osoby_na_kladce == 0) {
            int ocz2 = waiting_count_locked();
            if (ocz2 > 0) {
                int anul = cancel_waiting_groups_locked();
                unlock_sem(sem_id);
                cout << COL_GREEN << "[PRZEWODNIK T" << trasa << "] Alarm przed wyjsciem - anulowano " << anul << " osob" << endl;
                logf_simple("PRZEWODNIK", "Alarm: anulowanie kolejki");
                usleep(100000);
                continue;
            }
        }

        bool zrobiono = false;

        MsgExit msgExit{};
        unlock_sem(sem_id);

        if (msgrcv(msg_id, &msgExit, sizeof(msgExit) - sizeof(long), exit_mtype, IPC_NOWAIT) != -1) {
            int gsz = (msgExit.group_size > 0) ? msgExit.group_size : 1;

            lock_sem(sem_id);
            int kladka_przed = stan->osoby_na_kladce;
            int kier_przed = stan->kierunek_ruchu_kladka;
            
            if ((stan->osoby_na_kladce + gsz <= K) &&
                (stan->kierunek_ruchu_kladka == DIR_NONE || stan->kierunek_ruchu_kladka == DIR_LEAVING)) {

                stan->kierunek_ruchu_kladka = DIR_LEAVING;
                stan->osoby_na_kladce += gsz;
                (*wjask) -= gsz;
                if (*wjask < 0) *wjask = 0;
                int exit_kladka = stan->osoby_na_kladce;
                int exit_kier = stan->kierunek_ruchu_kladka;
                unlock_sem(sem_id);

                log_kladka("WCHODZI_NA_KLADKE", kladka_przed, kier_przed, exit_kladka, exit_kier);

                usleep((useconds_t)BRIDGE_DURATION_MS * 1000);

                lock_sem(sem_id);
                int kladka_przed2 = stan->osoby_na_kladce;
                int kier_przed2 = stan->kierunek_ruchu_kladka;
                stan->osoby_na_kladce -= gsz;
                (*bilety) -= gsz;
                if (*bilety < 0) *bilety = 0;
                if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;
                int exit_kladka_after = stan->osoby_na_kladce;
                int exit_kier_after = stan->kierunek_ruchu_kladka;

                cout << COL_GREEN << "[PRZEWODNIK T" << trasa << "]" << COL_RESET << " OPUSCIL jaskinie pid=" << msgExit.pid
                     << " | bilety=" << *bilety << " | kladka=" << exit_kladka_after << endl;

                char logbuf2[128];
                snprintf(logbuf2, sizeof(logbuf2), "OPUSCIL jaskinie kladka=%d", exit_kladka_after);
                logf_simple("PRZEWODNIK", logbuf2);
                unlock_sem(sem_id);

                log_kladka("ZSZEDL_Z_KLADKI", kladka_przed2, kier_przed2, exit_kladka_after, exit_kier_after);
                zrobiono = true;
            } else {
                unlock_sem(sem_id);
            }
        }

        if (!zrobiono && !alarm) {
            lock_sem(sem_id);
            GroupItem it{};
            bool from_prio = false;
            bool jest = (dequeue_group_locked(it, from_prio) == 0);
            int group_size = (it.group_size > 0) ? it.group_size : 1;
            int limit_trasy = (trasa == 1) ? N1 : N2;

            if (jest &&
                (*wjask + group_size <= limit_trasy) &&
                (stan->osoby_na_kladce + group_size <= K) &&
                (stan->kierunek_ruchu_kladka == DIR_NONE || stan->kierunek_ruchu_kladka == DIR_ENTERING)) {

                int kladka_przed = stan->osoby_na_kladce;
                int kier_przed = stan->kierunek_ruchu_kladka;
                
                stan->kierunek_ruchu_kladka = DIR_ENTERING;
                stan->osoby_na_kladce += group_size;
                int kladka_snap = stan->osoby_na_kladce;
                int kier_snap = stan->kierunek_ruchu_kladka;

                int kolejka_po = waiting_count_locked();
                unlock_sem(sem_id);

                log_kladka("WCHODZI_NA_KLADKE", kladka_przed, kier_przed, kladka_snap, kier_snap);

                cout << COL_GREEN << "[PRZEWODNIK T" << trasa << "]" << COL_RESET << " WEJSCIE na kladke | kolejka="
                     << kolejka_po << " | kladka=" << kladka_snap << "/" << K
                     << " | grupa=" << group_size << " | pid=" << it.pids[0] << endl;

                char logbuf[128];
                snprintf(logbuf, sizeof(logbuf), "WEJSCIE na kladke kladka=%d", kladka_snap);
                logf_simple("PRZEWODNIK", logbuf);

                for (int i = 0; i < group_size && i < 2; i++) {
                    if (it.pids[i] > 0) {
                        MsgEnter msgEnter{};
                        msgEnter.mtype = MSG_ENTER_BASE + it.pids[i];
                        msgEnter.trasa = trasa;
                        if (msgsnd(msg_id, &msgEnter, sizeof(msgEnter) - sizeof(long), 0) == -1) {
                            if (errno != EINTR) perror("msgsnd enter");
                        }
                    }
                }

                usleep((useconds_t)BRIDGE_DURATION_MS * 1000);

                lock_sem(sem_id);
                int kladka_przed2 = stan->osoby_na_kladce;
                int kier_przed2 = stan->kierunek_ruchu_kladka;
                stan->osoby_na_kladce -= group_size;
                (*wjask) += group_size;
                if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;
                int kladka_after = stan->osoby_na_kladce;
                int kier_after = stan->kierunek_ruchu_kladka;
                unlock_sem(sem_id);

                log_kladka("ZSZEDL_Z_KLADKI", kladka_przed2, kier_przed2, kladka_after, kier_after);

                cout << COL_GREEN << "[PRZEWODNIK T" << trasa << "]" << COL_RESET << " DOTARL do jaskini | w_jaskini=" << *wjask
                     << " | kladka=" << kladka_after << endl;

                logf_simple("PRZEWODNIK", "DOTARL do jaskini");
                zrobiono = true;
            } else if (jest) {
                if (trasa == 1) {
                    if (from_prio) q_push(stan->q_t1_prio, it);
                    else q_push(stan->q_t1, it);
                } else {
                    if (from_prio) q_push(stan->q_t2_prio, it);
                    else q_push(stan->q_t2, it);
                }
                unlock_sem(sem_id);
            } else {
                unlock_sem(sem_id);
            }
        }

        if (g_terminated) break;

        if (!zrobiono && koniec) {
            lock_sem(sem_id);
            bool pusto = (stan->osoby_na_kladce == 0 &&
                          (stan->q_t1.count + stan->q_t1_prio.count) == 0 &&
                          (stan->q_t2.count + stan->q_t2_prio.count) == 0 &&
                          stan->osoby_trasa1 == 0 && stan->osoby_trasa2 == 0);
            unlock_sem(sem_id);
            if (pusto) break;
        }

        usleep(100000);
    }

    lock_sem(sem_id);
    int anul = cancel_waiting_groups_locked();
    unlock_sem(sem_id);
    if (anul > 0) {
        cout << COL_GREEN << "[PRZEWODNIK T" << trasa << "]" << COL_RESET << " Koniec - anulowano " << anul << " oczekujacych" << endl;
    }

    if (shmdt(stan) == -1) perror("shmdt");
    return 0;
}
