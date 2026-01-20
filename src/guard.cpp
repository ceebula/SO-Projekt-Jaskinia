#include "common.hpp"
#include <iostream>
#include <cstdlib>

using namespace std;

int main() {
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    while (true) {
        sleep(5);
        int which = (rand() % 2) + 1;
        int sig = (which == 1) ? SIGUSR1 : SIGUSR2;

        if (which == 1) {
            cout << "[STRAZNIK] Wysylam sygnal T1" << endl;
            logf_simple("STRAZNIK", "Wysylam sygnal T1");
        } else {
            cout << "[STRAZNIK] Wysylam sygnal T2" << endl;
            logf_simple("STRAZNIK", "Wysylam sygnal T2");
        }

        if (kill(0, sig) == -1) {
            perror("kill");
        }
    }
    return 0;
}
