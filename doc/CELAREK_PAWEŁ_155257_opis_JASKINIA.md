# Temat 13 – Jaskinia
---
## Opis projektu

Do jaskini w godzinach od Tp do Tk organizowane są wycieczki dwiema trasami. Na każdą z tras mogą wchodzić grupy zwiedzających o liczebności N1 i N2. Wejście i wyjście z jaskini odbywa się kładkami o pojemności K (K<Ni) każda. Kładki te są bardzo wąskie, więc możliwy jest jedynie ruch w jedną stronę w danej chwili czasu.

Do każdej z tras próbują dostać się zwiedzający z tym, że nie może ich wejść więcej niż Ni, a wchodząc na daną trasę, na kładce nie może być ich równocześnie więcej niż K. W momencie wyruszania w daną trasę przewodnik musi dopilnować, aby na kładce nie było żadnego wchodzącego zwiedzającego. Jednocześnie musi dopilnować by liczba zwiedzających na danej trasie nie przekroczyła Ni. Chętni na wycieczkę w różnym wieku (od 1 roku do 80 lat), pojawiają się w sposób losowy co pewien czas.

Zwiedzanie trasą 1 trwa określoną ilość czasu równą T1, zwiedzanie trasą 2 trwa określoną ilość czasu równą T2. Po dotarciu do wyjścia zwiedzający przez kładkę wychodzą z jaskini. Po opuszczeniu trasy przez ostatniego zwiedzającego, kolejni zwiedzający próbują wejść do jaskini (kładka jest na tyle wąska, że ruch w danym momencie może odbywać się tylko w jedną stronę).

Osoba, która chce uczestniczyć w wycieczce kupuje bilet, który upoważnia ją do korzystania z jednej tras zgodnie z regulaminem:
* Dzieci poniżej 3 roku życia nie płacą za bilet;
* Dzieci poniżej 8 roku życia muszą przebywać pod opieką osoby dorosłej i mogą zwiedzać jaskinię wraz z opiekunem jedynie trasą nr2;
* Osoby powyżej 75 roku życia mogą zwiedzać jaskinię jedynie trasą nr2;
* Osoby, które chcą powtórzyć wycieczkę w tym samym dniu (ok. 10%) mogą zakupić bilet z 50% zniżką i wejść na drugą trasę (inną niż pierwsza, jeżeli jest to zgodne z regulaminem) omijając kolejkę.

Każda grupa musi przerwać wykonywanie wycieczek przed czasem Tk po otrzymaniu polecenia (sygnał1 dla przewodnika na trasie nr1, sygnał2 dla przewodnika na trasie nr2) od strażnika (jeżeli to polecenie nastąpi przed wyjściem w trasę, przewodnik nie prowadzi grupy, a zwiedzający opuszczają jaskinię. Jeżeli polecenie dotrze do przewodnika w trakcie zwiedzania, grupa kończy zwiedzanie normalnie).

Programy przewodnika, kasjera, zwiedzającego i strażnika, mają zagwarantować sprawną obsługę i zwiedzanie zgodnie z podanym regulaminem. Raport z przebiegu symulacji zapisać w pliku (plikach) tekstowym.

---

## Planowane testy

Celem testów jest weryfikacja poprawności synchronizacji procesów, obsługi zasobów współdzielonych (pamięć dzielona, semafory/kolejki) oraz reakcji na sygnały systemowe.

### Test 1: Weryfikacja przepustowości kładek (Problem wąskiego gardła)
* **Cel:** Sprawdzenie, czy mechanizm synchronizacji poprawnie limituje liczbę osób na kładce (`K`) i zapobiega wejściu na nią więcej niż dopuszczalnej liczbie osób jednocześnie, mimo dużej liczby chętnych (`N1`, `N2` > `K`).
* **Scenariusz:** Uruchomienie symulacji z dużą liczbą zwiedzających próbujących wejść do jaskini jednocześnie. Obserwacja liczników osób znajdujących się na kładce w logach.
* **Oczekiwany rezultat:** W logach programu widać, że w jednym momencie na kładce znajduje się maksymalnie `K` procesów. Kolejne procesy są blokowane (czekają w kolejce semafora) aż do zwolnienia miejsca, nie dochodzi do przekroczenia limitu.

### Test 2: Synchronizacja ruchu jednokierunkowego (Wejście vs Wyjście)
* **Cel:** Upewnienie się, że nie dochodzi do zakleszczenia (deadlock) lub kolizji, gdy grupa chce wejść do jaskini, podczas gdy inna grupa próbuje z niej wyjść tą samą wąską kładką.
* **Scenariusz:** Symulacja sytuacji, w której grupa kończy zwiedzanie trasy (próba wejścia na kładkę w celu wyjścia), podczas gdy w kolejce czekają już nowi chętni na wejście.
* **Oczekiwany rezultat:** Ruch odbywa się wyłącznie w jedną stronę. Procesy wchodzące zostają wstrzymane do momentu, aż kładka zostanie całkowicie opróżniona przez procesy wychodzące (lub odwrotnie).

### Test 3: Weryfikacja logiki biznesowej i ograniczeń wiekowych
* **Cel:** Sprawdzenie poprawności algorytmu decyzyjnego Kasjera/Przewodnika w zakresie przydziału tras zgodnie z regulaminem.
* **Scenariusz:** Wygenerowanie procesów o specyficznym wieku:
    1. Dziecko (np. 5 lat) z opiekunem próbujące wejść na Trasę 1.
    2. Senior (80 lat) próbujący wejść na Trasę 1.
    3. Klient powracający (10% szansy) próbujący ominąć kolejkę.
* **Oczekiwany rezultat:**
    * Dziecko z opiekunem oraz Senior otrzymują odmowę wejścia na Trasę 1 lub są automatycznie przekierowani na Trasę 2.
    * Klient z biletem zniżkowym jest obsługiwany priorytetowo (omija standardową kolejkę oczekujących).

### Test 4: Obsługa sygnałów i zamykanie systemu (Graceful Shutdown)
* **Cel:** Weryfikacja reakcji na sygnały od Strażnika oraz poprawnego czyszczenia zasobów IPC.
* **Scenariusz:** Wysłanie sygnału (np. `SIGUSR1` / `SIGUSR2`) w dwóch momentach:
    1. Gdy grupa czeka na wejście (przed trasą).
    2. Gdy grupa jest w trakcie zwiedzania.
* **Oczekiwany rezultat:**
    1. Grupa oczekująca natychmiast rezygnuje/opuszcza jaskinię.
    2. Grupa zwiedzająca kończy trasę normalnie, a następnie opuszcza jaskinię.
    * Po zakończeniu działania programu wszystkie zasoby (semafory, pamięć dzielona, kolejki komunikatów) są usunięte z systemu.

---

## Repozytorium GitHub
[(https://github.com/ceebula/SO-Projekt-Jaskinia)]

