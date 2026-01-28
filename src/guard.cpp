#include "common.hpp"
#include <iostream>
#include <cstdlib>

using namespace std;

static volatile sig_atomic_t g_terminated = 0;
static bool signal_t1_sent = false;
static bool signal_t2_sent = false;

static void handle_term(int) {
    g_terminated = 1;
}

int main() {
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGTERM, handle_term);

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

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

    cout << "[STRAZNIK] Gotowy, czekam na godzine zamkniecia" << endl;

    while (!g_terminated) {
        usleep(500000);
        if (g_terminated) break;

        lock_sem(sem_id);
        time_t start = stan->start_time;
        int opening = stan->sim_opening_hour;
        int closing = stan->sim_closing_hour;
        pid_t target_t1 = stan->przewodnik_t1_pid;
        pid_t target_t2 = stan->przewodnik_t2_pid;
        unlock_sem(sem_id);

        time_t now = time(NULL);
        int elapsed = (int)(now - start);
        int current_hour = opening + (elapsed / SECONDS_PER_HOUR);

        int hours_before_close = 1;
        bool should_send_signal = (current_hour >= closing - hours_before_close);

        if (should_send_signal && !signal_t1_sent && target_t1 > 0) {
            cout << "[STRAZNIK] Godzina " << current_hour << ":00 - Wysylam sygnal T1 (zamkniecie o " << closing << ":00)" << endl;
            logf_simple("STRAZNIK", "Sygnal T1 przed zamknieciem");
            if (kill(target_t1, SIGUSR1) == -1) {
                if (errno != ESRCH) perror("kill T1");
            }
            signal_t1_sent = true;
        }

        if (should_send_signal && !signal_t2_sent && target_t2 > 0) {
            cout << "[STRAZNIK] Godzina " << current_hour << ":00 - Wysylam sygnal T2 (zamkniecie o " << closing << ":00)" << endl;
            logf_simple("STRAZNIK", "Sygnal T2 przed zamknieciem");
            if (kill(target_t2, SIGUSR2) == -1) {
                if (errno != ESRCH) perror("kill T2");
            }
            signal_t2_sent = true;
        }

        if (signal_t1_sent && signal_t2_sent) {
            cout << "[STRAZNIK] Oba sygnaly wyslane, koncze prace" << endl;
            break;
        }
    }

    if (shmdt(stan) == -1) perror("shmdt");
    return 0;
}
