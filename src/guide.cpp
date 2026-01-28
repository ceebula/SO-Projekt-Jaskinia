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
    stan->alarm_do_t1 = time(NULL) + ALARM_SECONDS;
    unlock_sem(sem_id);
    cout << "[PRZEWODNIK T1] Alarm! Blokada wejsc na " << ALARM_SECONDS << "s" << endl;
    logf_simple("PRZEWODNIK", "Alarm T1");
}

static void alarm_t2(int) {
    if (!stan) return;
    lock_sem(sem_id);
    stan->alarm_t2 = 1;
    stan->alarm_do_t2 = time(NULL) + ALARM_SECONDS;
    unlock_sem(sem_id);
    cout << "[PRZEWODNIK T2] Alarm! Blokada wejsc na " << ALARM_SECONDS << "s" << endl;
    logf_simple("PRZEWODNIK", "Alarm T2");
}

static bool alarm_aktywny_locked(time_t now) {
    if (trasa == 1) {
        if (stan->alarm_t1 && now >= stan->alarm_do_t1) stan->alarm_t1 = 0;
        return stan->alarm_t1 != 0;
    } else {
        if (stan->alarm_t2 && now >= stan->alarm_do_t2) stan->alarm_t2 = 0;
        return stan->alarm_t2 != 0;
    }
}

static void notify_cancel(pid_t pid) {
    if (pid <= 0) return;
    MsgEnter msgCancel{};
    msgCancel.mtype = MSG_ENTER_BASE + pid;
    msgCancel.trasa = -1;
    msgsnd(msg_id, &msgCancel, sizeof(msgCancel) - sizeof(long), IPC_NOWAIT);
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

static int dequeue_group_locked(GroupItem& out) {
    if (trasa == 1) {
        if (stan->q_t1_prio.count > 0) return q_pop(stan->q_t1_prio, out);
        if (stan->q_t1.count > 0) return q_pop(stan->q_t1, out);
        return -1;
    } else {
        if (stan->q_t2_prio.count > 0) return q_pop(stan->q_t2_prio, out);
        if (stan->q_t2.count > 0) return q_pop(stan->q_t2, out);
        return -1;
    }
}

static int waiting_count_locked() {
    if (trasa == 1) return stan->q_t1.count + stan->q_t1_prio.count;
    return stan->q_t2.count + stan->q_t2_prio.count;
}

__attribute__((unused))
static void enqueue_return_other_route_locked() {
    int other = (trasa == 1) ? 2 : 1;
    GroupItem it{};
    it.group_size = 1;

    if (other == 1) {
        if (stan->bilety_sprzedane_t1 + 1 <= N1) {
            if (q_push(stan->q_t1_prio, it) == 0) {
                stan->bilety_sprzedane_t1 += 1;
                stan->oczekujacy_t1 = stan->q_t1.count + stan->q_t1_prio.count;
                logf_simple("PRZEWODNIK", "Powrot: priorytet na T1");
                cout << "[PRZEWODNIK T" << trasa << "] POWROT -> priorytet na T1" << endl;
            }
        }
    } else {
        if (stan->bilety_sprzedane_t2 + 1 <= N2) {
            if (q_push(stan->q_t2_prio, it) == 0) {
                stan->bilety_sprzedane_t2 += 1;
                stan->oczekujacy_t2 = stan->q_t2.count + stan->q_t2_prio.count;
                logf_simple("PRZEWODNIK", "Powrot: priorytet na T2");
                cout << "[PRZEWODNIK T" << trasa << "] POWROT -> priorytet na T2" << endl;
            }
        }
    }
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

    cout << (trasa == 1 ? "[PRZEWODNIK T1] Gotowy" : "[PRZEWODNIK T2] Gotowy") << endl;

    long exit_mtype = (trasa == 1) ? MSG_EXIT_T1 : MSG_EXIT_T2;

    while (!g_terminated) {
        time_t now = time(NULL);

        lock_sem(sem_id);
        bool koniec = (stan->end_time != 0 && now >= stan->end_time);
        bool alarm = alarm_aktywny_locked(now);

        int* wjask = (trasa == 1) ? &stan->osoby_trasa1 : &stan->osoby_trasa2;
        int* bilety = (trasa == 1) ? &stan->bilety_sprzedane_t1 : &stan->bilety_sprzedane_t2;

        int ocz = waiting_count_locked();
        if (trasa == 1) stan->oczekujacy_t1 = ocz;
        else stan->oczekujacy_t2 = ocz;

        bool kier_none = (stan->kierunek_ruchu_kladka == DIR_NONE);

        if (alarm && *wjask == 0 && stan->osoby_na_kladce == 0) {
            int ocz2 = waiting_count_locked();
            if (ocz2 > 0) {
                int anul = cancel_waiting_groups_locked();
                unlock_sem(sem_id);
                cout << "[PRZEWODNIK T" << trasa << "] Alarm przed wyjsciem - anulowano " << anul << " osob" << endl;
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
            if ((stan->osoby_na_kladce + gsz <= K) &&
                (stan->kierunek_ruchu_kladka == DIR_NONE || stan->kierunek_ruchu_kladka == DIR_LEAVING)) {

                stan->kierunek_ruchu_kladka = DIR_LEAVING;
                stan->osoby_na_kladce += gsz;
                (*wjask) -= gsz;
                if (*wjask < 0) *wjask = 0;
                unlock_sem(sem_id);

                usleep((useconds_t)BRIDGE_DURATION_MS * 1000);

                lock_sem(sem_id);
                stan->osoby_na_kladce -= gsz;
                (*bilety) -= gsz;
                if (*bilety < 0) *bilety = 0;
                if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;

                cout << "[PRZEWODNIK T" << trasa << "] OPUSCIL jaskinie pid=" << msgExit.pid
                     << " | bilety=" << *bilety << " | kladka=" << stan->osoby_na_kladce << endl;

                logf_simple("PRZEWODNIK", "OPUSCIL jaskinie");
                unlock_sem(sem_id);
                zrobiono = true;
            } else {
                unlock_sem(sem_id);
            }
        }

        if (!zrobiono && !alarm) {
            lock_sem(sem_id);
            GroupItem it{};
            bool jest = (dequeue_group_locked(it) == 0);
            int group_size = (it.group_size > 0) ? it.group_size : 1;
            int limit_trasy = (trasa == 1) ? N1 : N2;

            if (jest &&
                (*wjask + group_size <= limit_trasy) &&
                (stan->osoby_na_kladce + group_size <= K) &&
                (kier_none || stan->kierunek_ruchu_kladka == DIR_ENTERING)) {

                stan->kierunek_ruchu_kladka = DIR_ENTERING;
                stan->osoby_na_kladce += group_size;

                int kolejka_po = waiting_count_locked();
                unlock_sem(sem_id);

                cout << "[PRZEWODNIK T" << trasa << "] WEJSCIE na kladke | kolejka="
                     << kolejka_po << " | kladka=" << stan->osoby_na_kladce << "/" << K
                     << " | grupa=" << group_size << " | pid=" << it.pids[0] << endl;

                logf_simple("PRZEWODNIK", "WEJSCIE na kladke");

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
                stan->osoby_na_kladce -= group_size;
                (*wjask) += group_size;
                if (stan->osoby_na_kladce == 0) stan->kierunek_ruchu_kladka = DIR_NONE;

                cout << "[PRZEWODNIK T" << trasa << "] DOTARL do jaskini | w_jaskini=" << *wjask
                     << " | kladka=" << stan->osoby_na_kladce << endl;

                logf_simple("PRZEWODNIK", "DOTARL do jaskini");

                unlock_sem(sem_id);
                zrobiono = true;
            } else if (jest) {
                if (trasa == 1) q_push(stan->q_t1, it);
                else q_push(stan->q_t2, it);
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
        cout << "[PRZEWODNIK T" << trasa << "] Koniec - anulowano " << anul << " oczekujacych" << endl;
    }

    if (shmdt(stan) == -1) perror("shmdt");
    return 0;
}
