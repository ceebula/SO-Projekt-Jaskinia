#include "common.hpp"
#include <iostream>

using namespace std;

static volatile sig_atomic_t g_terminated = 0;

static void handle_term(int) {
    g_terminated = 1;
}

int main() {
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGTERM, handle_term);

    key_t key = ftok(FTOK_FILE, SHM_ID);
    if (key == -1) die_perror("ftok SHM");
    int shm_id = shmget(key, sizeof(JaskiniaStan), 0600);
    if (shm_id == -1) die_perror("shmget");
    JaskiniaStan* stan = (JaskiniaStan*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) die_perror("shmat");

    key = ftok(FTOK_FILE, SEM_ID);
    if (key == -1) die_perror("ftok SEM");
    int sem_id = semget(key, 1, 0600);
    if (sem_id == -1) die_perror("semget");

    key = ftok(FTOK_FILE, MSG_ID);
    if (key == -1) die_perror("ftok MSG");
    int msg_id = msgget(key, 0600);
    if (msg_id == -1) die_perror("msgget");

    Wiadomosc msg;
    while (!g_terminated) {
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), MSG_KASJER, 0) == -1) {
            if (g_terminated) break;
            if (errno == EINTR) continue;
            perror("msgrcv");
            break;
        }

        bool ok = true;

        if (msg.wiek < 1 || msg.wiek > 80) ok = false;
        if (msg.typ_biletu != 1 && msg.typ_biletu != 2) ok = false;

        int gsz = msg.group_size;
        if (gsz != 1 && gsz != 2) ok = false;

        if (ok) {
            if (msg.wiek < 8) {
                if (msg.typ_biletu != 2) ok = false;
                if (gsz != 2) ok = false;
            } else {
                if (gsz != 1) ok = false;
                if (msg.wiek > 75 && msg.typ_biletu != 2) ok = false;
            }
        }

        lock_sem(sem_id);

        time_t now = time(NULL);
        if (stan->end_time != 0 && now >= stan->end_time) ok = false;

        if (ok) {
            if (msg.typ_biletu == 1 && stan->alarm_t1) ok = false;
            if (msg.typ_biletu == 2 && stan->alarm_t2) ok = false;
        }

        if (ok) {
            if (msg.typ_biletu == 1) {
                if (stan->bilety_sprzedane_t1 + gsz <= N1) stan->bilety_sprzedane_t1 += gsz;
                else ok = false;
            } else {
                if (stan->bilety_sprzedane_t2 + gsz <= N2) stan->bilety_sprzedane_t2 += gsz;
                else ok = false;
            }
        }

        if (ok) {
            GroupItem it{};
            it.group_size = gsz;
            it.pids[0] = msg.pids[0];
            it.pids[1] = msg.pids[1];

            int rc = 0;
            if (msg.typ_biletu == 1) {
                if (msg.powrot) rc = q_push(stan->q_t1_prio, it);
                else rc = q_push(stan->q_t1, it);
                stan->oczekujacy_t1 = stan->q_t1.count + stan->q_t1_prio.count;
            } else {
                if (msg.powrot) rc = q_push(stan->q_t2_prio, it);
                else rc = q_push(stan->q_t2, it);
                stan->oczekujacy_t2 = stan->q_t2.count + stan->q_t2_prio.count;
            }
            if (rc == -1) ok = false;
        }

        unlock_sem(sem_id);

        if (ok) {
            char b[256];
            snprintf(b, sizeof(b), "Bilet OK: T%d grupa=%d wiek=%d", msg.typ_biletu, msg.group_size, msg.wiek);
            logf_simple("KASJER", b);
        } else {
            logf_simple("KASJER", "Odmowa biletu");
        }

        msg.mtype = msg.id_nadawcy;
        msg.odpowiedz = ok;

        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            if (errno == EINTR) continue;
            perror("msgsnd");
            break;
        }
    }

    if (shmdt(stan) == -1) perror("shmdt");
    return 0;
}
