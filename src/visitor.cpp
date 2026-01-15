#include "common.hpp"
#include <iostream>

using namespace std;

int main() {
    //test czy proces wstaje
    cout << "[TURYSTA " << getpid() << "] przyszedł" << endl;
    return 0;
}