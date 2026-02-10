// guard.cpp - Proces strażnika (wysyła SIGUSR1/SIGUSR2 przed zamknięciem)
#include "common.hpp"
#include <iostream>
#include <cstdlib>

using namespace std;

static volatile sig_atomic_t g_terminated = 0;
static volatile sig_atomic_t g_user_shutdown = 0;
static bool signal_t1_sent = false;
static bool signal_t2_sent = false;

static void handle_term(int) {
    g_terminated = 1;
}

static void handle_usr1(int) {
    g_user_shutdown = 1;
}

int main() {
    signal(SIGUSR1, handle_usr1);
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

    cout << COL_RED << "[STRAZNIK]" << COL_RESET << " Gotowy, czekam na godzine zamkniecia" << endl;

    // Strażnik wysyła sygnały przed zamknięciem
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
        // === ŁAŃCUCH SYGNAŁÓW ZAMKNIĘCIA ===
        // Zamknięcie inicjowane przez: czas (godzina Tk-1) LUB użytkownika (pkill -USR1)
        // Strażnik → SIGUSR1 do przewodnika T1, SIGUSR2 do T2
        // Po opróżnieniu jaskini → SIGTERM do main → cleanup()
        bool time_to_close = (current_hour >= closing - hours_before_close);
        bool user_wants_shutdown = (g_user_shutdown != 0);

        #ifndef TEST_5000
        bool should_send_signal = time_to_close || user_wants_shutdown;
        #else
        bool should_send_signal = 0;
        #endif
        

        
        // Jeśli użytkownik zażądał zakończenia, zaloguj to
        if (user_wants_shutdown && !signal_t1_sent && !signal_t2_sent) {
            cout << COL_RED << "[STRAZNIK]" << COL_RESET << " Otrzymano SIGUSR1" << endl;
            logf_simple("STRAZNIK", "Uzytkownik zazadal zamkniecia (SIGUSR1)");
        }

        if (should_send_signal && !signal_t1_sent && target_t1 > 0) {
            cout << COL_RED << "[STRAZNIK]" << COL_RESET << " Godzina " << current_hour << ":00 - Wysylam sygnal T1 (zamkniecie o " << closing << ":00)" << endl;
            logf_simple("STRAZNIK", "Sygnal T1 przed zamknieciem");
            if (kill(target_t1, SIGUSR1) == -1) {
                if (errno != ESRCH) perror("kill T1");
            }
            signal_t1_sent = true;
        }

        if (should_send_signal && !signal_t2_sent && target_t2 > 0) {
            cout << COL_RED << "[STRAZNIK]" << COL_RESET << " Godzina " << current_hour << ":00 - Wysylam sygnal T2 (zamkniecie o " << closing << ":00)" << endl;
            logf_simple("STRAZNIK", "Sygnal T2 przed zamknieciem");
            if (kill(target_t2, SIGUSR2) == -1) {
                if (errno != ESRCH) perror("kill T2");
            }
            signal_t2_sent = true;
        }

        if (signal_t1_sent && signal_t2_sent) {
            cout << COL_RED << "[STRAZNIK]" << COL_RESET << " Oba sygnaly wyslane, koncze prace" << endl;
            logf_simple("STRAZNIK", "Oczekiwanie na zakonczenie pracy przewodnikow...");
            
            // Czekamy aż przewodnicy zakończą obsługę turystów
            int wait_count = 0;
            const int max_wait = 60;  // 30 sekund (60 * 500ms)
            while (wait_count < max_wait) {
                usleep(500000);
                lock_sem(sem_id);
                int w1 = stan->osoby_trasa1;
                int w2 = stan->osoby_trasa2;
                int k1 = stan->osoby_na_kladce[0];
                int k2 = stan->osoby_na_kladce[1];
                pid_t main_pid = getppid();
                unlock_sem(sem_id);
                
                // Jeśli jaskinia i kładki są puste - możemy kończyć
                if (w1 == 0 && w2 == 0 && k1 == 0 && k2 == 0) {
                    cout << COL_RED << "[STRAZNIK]" << COL_RESET << " Jaskinia pusta - wysylam SIGTERM do main" << endl;
                    logf_simple("STRAZNIK", "Koniec symulacji - SIGTERM do main");
                    kill(main_pid, SIGTERM);
                    break;
                }
                wait_count++;
            }
            
            if (wait_count >= max_wait) {
                cout << COL_RED << "[STRAZNIK]" << COL_RESET << " Timeout - wymuszam zakonczenie" << endl;
                kill(getppid(), SIGTERM);
            }
            break;
        }
    }

    if (shmdt(stan) == -1) perror("shmdt");
    return 0;
}
