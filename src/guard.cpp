#include "common.hpp"
#include <iostream>
#include <cstdlib>
#include <ctime>

using namespace std;

int main() {
    srand(time(NULL) ^ getpid());

    while (true) {
        sleep(5 + rand() % 5);

        int sig = (rand() % 2) ? SIGUSR1 : SIGUSR2;
        cout << "[STRAZNIK] Wysylam sygnal "
             << (sig == SIGUSR1 ? "T1" : "T2") << endl;

        kill(0, sig);
    }
}
