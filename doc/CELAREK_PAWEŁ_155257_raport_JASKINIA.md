# Raport z projektu: Symulacja Jaskini

**Autor:** Paweł Celarek 
**Numer albumu:** 155257 
**Temat:** 13 - Jaskinia 
**Repozytorium:** [https://github.com/ceebula/SO-Projekt-Jaskinia](https://github.com/ceebula/SO-Projekt-Jaskinia)

---

## 1. Założenia projektowe

### 1.1. Opis problemu
Symulacja wycieczek do jaskini z dwoma trasami (T1, T2). Kluczowe ograniczenia:
- Dwie kładki (po jednej na trasę) o pojemności K < N każda, ruch tylko w jednym kierunku w danym momencie
- Limity osób na trasach: N1, N2
- Godziny pracy: Tp-Tk (symulowane)
- Regulamin: dzieci <3 bezpłatnie, dzieci <8 tylko T2 z opiekunem, seniorzy 76+ tylko T2, powroty 10% ze zniżką 50% i priorytetem

### 1.2. Model wieloprocesowy
Zastosowano architekturę `fork()`/`exec()` z pięcioma typami procesów:
- **main.cpp** - kontroler: tworzy zasoby IPC, spawnuje procesy potomne, sprząta zasoby przy zakończeniu
- **cashier.cpp** - kasjer: walidacja regulaminu, sprzedaż biletów, wstawianie do kolejki w pamięci dzielonej
- **guide.cpp** - przewodnik (x2): obsługa tras T1/T2, synchronizacja ruchu na kładce
- **guard.cpp** - strażnik: wysyła sygnały zamknięcia (SIGUSR1/SIGUSR2) przed godziną Tk
- **visitor.cpp** - zwiedzający: losowy wiek, zakup biletu, zwiedzanie trasy

### 1.3. Mechanizmy IPC
| Mechanizm | Zastosowanie |
|-----------|--------------|
| Pamięć dzielona SysV (`shmget`/`shmat`) | Wspólny stan jaskini - kolejki turystów, liczniki osób, flagi alarmów |
| Semafor SysV (`semget`/`semop`) | Mutex chroniący dostęp do pamięci dzielonej |
| Kolejka komunikatów (`msgget`/`msgsnd`/`msgrcv`) | Komunikacja: turysta->kasjer (żądanie biletu), przewodnik->turysta (powiadomienie o wejściu/anulowaniu), turysta->przewodnik (zgłoszenie zakończenia trasy) |
| Sygnały (`kill`/`signal`) | SIGUSR1/SIGUSR2 od strażnika do przewodników (alarm zamknięcia), SIGTERM/SIGINT do kończenia procesów |
| Pipe (`pipe`/`read`/`write`) | Synchronizacja dziecko↔opiekun (grupa 2-osobowa) |

### 1.4. Parametry symulacji (przykładowe wartości)
| Parametr | Opis |
|----------|------|
| N1, N2 | Maksymalna liczba osób na trasie |
| K | Maksymalna liczba osób na kładce jednocześnie (K < N) |
| T1_MS, T2_MS | Czas zwiedzania trasy [ms] |
| BRIDGE_DURATION_MS | Czas przejścia przez kładkę [ms] |

---

## 2. Ogólny opis kodu


### 2.1. Synchronizacja kładek
Każda trasa posiada własną kładkę z niezależną synchronizacją. Stan kładek przechowywany w tablicach w pamięci dzielonej:
- `osoby_na_kladce[2]` - liczba osób na każdej kładce (0..K)
- `kierunek_ruchu_kladka[2]` - aktualny kierunek ruchu (enum: `DIR_NONE`, `DIR_ENTERING`, `DIR_LEAVING`)

Jak to działa w praktyce:
1. Sprawdzenie czy kładka jest wolna (`kierunek == DIR_NONE`) lub ruch w zgodnym kierunku
2. Jeśli tak - ustawienie kierunku i zwiększenie licznika osób na kładce
3. Symulacja przejścia przez kładkę (`usleep()`)
4. Zmniejszenie licznika, a gdy `osoby_na_kladce == 0` -> reset kierunku do `DIR_NONE`

### 2.2. Grupy dziecko+opiekun (synchronizacja przez pipe)
Dziecko <8 lat tworzy proces opiekuna przez `fork()` + `execl()`. Synchronizacja przez łącze nienazwane:
1. Dziecko tworzy `pipe()` przed `fork()`
2. Dziecko kupuje bilet dla grupy 2-osobowej
3. Po otrzymaniu biletu dziecko sygnalizuje opiekunowi przez `write()` do pipe
4. Opiekun czeka na `read()` i dopiero wtedy odbiera komunikat wejścia na trasę
5. Oba procesy zwiedzają trasę równolegle, wychodzą niezależnie

### 2.3. Kolejki priorytetowe (powroty ze zniżką)
Powracający turyści (~10%) trafiają do osobnej kolejki `q_t*_prio`. Przewodnik najpierw sprawdza kolejkę priorytetową, dzięki czemu powracający omijają zwykłą kolejkę.

---

## 3. Co udało się zrobić

**Co działa:**
- Pełna symulacja jaskini zgodna z opisem tematu
- Dwie niezależne trasy z własnymi kładkami i limitami osób
- Regulamin wiekowy (dzieci <3 bezpłatnie, dzieci <8 tylko T2 z opiekunem, seniorzy 76+ tylko T2)
- Powroty ze zniżką 50% i kolejką priorytetową
- Sygnały SIGUSR1/SIGUSR2 do zamykania wycieczek
- Logowanie do plików (`symulacja.log`, `kladka_t1.log`, `kladka_t2.log`)
- Kolorowanie wyjścia terminala (kody ANSI)
- Podsumowanie finansowe na końcu symulacji

---

## 4. Napotkane problemy

### 4.1. Wyścigi przy dostępie do pamięci dzielonej
**Problem:** Kasjer i przewodnicy jednocześnie modyfikowali liczniki w pamięci dzielonej. Bez synchronizacji zdarzały się niespójności - np. licznik `osoby_na_kladce` przyjmował wartości ujemne.

**Rozwiązanie:** Semafor binarny jako mutex. Każda operacja na strukturze `JaskiniaStan` otoczona `lock_sem()`/`unlock_sem()`.

### 4.2. Deadlock na kładce
**Problem:** Gdy grupa wchodząca i wychodząca próbowały jednocześnie zająć kładkę, dochodziło do zakleszczenia - obie czekały na zwolnienie kładki przez drugą.

**Rozwiązanie:** Trzy stany kierunku kładki (`DIR_NONE`, `DIR_ENTERING`, `DIR_LEAVING`). Nowa grupa może wejść tylko gdy kierunek jest zgodny lub kładka pusta. Po opróżnieniu kładki kierunek resetowany do `DIR_NONE`.

### 4.3. Procesy zawieszające się na msgrcv()
**Problem:** Turysta czekający na `msgrcv()` (wywołanie blokujące) zawieszał się na zawsze, gdy kolejka została anulowana przed dostarczeniem komunikatu.

**Rozwiązanie:** Przy anulowaniu przewodnik wysyła do każdego PID-a z kolejki komunikat z `trasa = -1`. Turysta odbiera ten komunikat i kończy proces zamiast wisieć.

### 4.4. Synchronizacja grupy dziecko+opiekun
**Problem:** Dwa procesy (dziecko i opiekun) musiały wejść razem na kładkę jako jedna grupa, ale były to osobne procesy z własnymi PID-ami.

**Rozwiązanie:** Synchronizacja przez `pipe()`. Dziecko tworzy pipe przed `fork()`, po otrzymaniu biletu wysyła bajt przez `write()`. Opiekun czeka na `read()` zanim odbierze komunikat wejścia.

### 4.5. Zasoby IPC pozostające po Ctrl+C
**Problem:** Po przerwaniu symulacji przez Ctrl+C zasoby IPC (semafory, pamięć dzielona, kolejki) pozostawały w systemie (widoczne w `ipcs`).

**Rozwiązanie:** Handler `SIGINT` w main.cpp wywołuje funkcję `cleanup()` która:
- Wysyła `SIGTERM` do wszystkich procesów potomnych
- Zbiera procesy zombie przez `waitpid()`
- Usuwa zasoby IPC przez `shmctl(IPC_RMID)`, `semctl(IPC_RMID)`, `msgctl(IPC_RMID)`

---

## 5. Testy

Testy wykonano manualnie na systemie Linux. Każdy test ma jasno określony cel (co może się zepsuć) oraz weryfikację przez logi (`symulacja.log`, `kladka_t1.log`, `kladka_t2.log`) i stan systemu (`ps`, `ipcs`). Wszystkie poniższe testy zakończyły się **pozytywnie**.

### 5.1. Test obciążeniowy: 5000 procesów (spójność IPC + brak zakleszczeń)

**Cel:** wykrycie problemów pod obciążeniem: utrata wiadomości w kolejce komunikatów, niespójności w pamięci dzielonej (brak ochrony semaforem), zakleszczenia na kładkach, procesy zombie, zostawione zasoby IPC.

**Przygotowanie (tryb testowy):**
- Dla ułatwienia testowania - odkomentować linię (`// #define TEST_5000`)
- `N1 = N2 = 500`, `K = 50`
- `MAX_VISITORS` oraz pojemności kolejek (`QCAP`) ustawione na wartości dużo większe niż 5000 (żeby test ograniczała logika, nie limity bufora)
- w `main.cpp` generowanie dokładnie 5000 turystów (pętla) i brak przedwczesnego kończenia symulacji
- w `guard.cpp` wyłączone automatyczne zakończenie w trakcie testu (`should_send_signal = 0`)
- w `visitor.cpp` ustawiony wiek tak, aby **nie** tworzyć opiekunów (np. losowanie od 8 lat) i zablokowane powroty, aby każdy turysta wszedł dokładnie raz
- zakomentowane opóźnienia (`sleep/usleep`) w procesach krytycznych, aby wymusić dużą liczbę operacji IPC na sekundę

**Przebieg:**
1. Uruchomienie:
 ```bash
 cd build
 ./SymulacjaJaskini
 ```
2. Po zespawnowaniu turystów w losowym momencie zatrzymanie symulacji `Ctrl+Z` i kontrola liczby procesów:
 ```bash
 ps -ef | grep Zwiedzajacy | grep -v grep | wc -l
 ```
 Oczekiwany wynik: `5000`.
3. Ponowna kontrola podczas pauzy (żeby wykluczyć „gubienie procesów”): nadal `5000`.
4. Wznowienie `fg` i obserwacja, czy procesy turystów schodzą do `0`.

**Weryfikacja (logi + stan systemu):**
```bash
grep -c "Wejście na trasę" symulacja.log
grep -c "Zakończenie trasy" symulacja.log

ps aux | awk '$8 ~ /Z/' | wc -l
ipcs
```

**Kryteria zaliczenia:**
- `Wejście` = `5000` oraz `Zakończenie` = `5000`,
- brak zombie (`0`),
- brak zakleszczeń (symulacja dochodzi do końca),
- po zakończeniu `ipcs` nie pokazuje pozostałych zasobów IPC.

**Wynik:** ✅ PASS

---

### 5.2. Test przepustowości kładek (limit K)

**Cel:** potwierdzenie, że na kładce nigdy nie ma jednocześnie więcej niż `K` osób oraz że licznik nie „przeskakuje” (wyścigi / błędna synchronizacja).

**Przebieg:**
- uruchomienie symulacji w konfiguracji testowej (jak w 5.1),
- analiza logów kładek.

**Weryfikacja:**
```bash
grep -oP 'kladka=\d+->\K\d+' kladka_t1.log | sort -n | tail -1
grep -oP 'kladka=\d+->\K\d+' kladka_t2.log | sort -n | tail -1
```

**Kryterium zaliczenia:** maksymalna wartość w logu <= `K`.

**Wynik:** ✅ PASS

---

### 5.3. Test ruchu jednokierunkowego na kładkach (brak IN↔OUT bez opróżnienia)

**Cel:** sprawdzenie, czy kładka nie zmienia kierunku bezpośrednio z `IN` na `OUT` (lub odwrotnie) bez stanu pośredniego `NONE` (kładka pusta). To wykrywa typowe błędy synchronizacji prowadzące do zakleszczeń.

**Weryfikacja:**
```bash
grep -E 'kierunek=(IN|OUT)->(OUT|IN)' kladka_t1.log | wc -l
grep -E 'kierunek=(IN|OUT)->(OUT|IN)' kladka_t2.log | wc -l
```

**Kryterium zaliczenia:** `0` takich przejść w obu logach.

**Wynik:** ✅ PASS

---

### 5.4. Test pipe: wymuszone grupy dziecko+opiekun (synchronizacja dwóch procesów)

**Cel:** potwierdzenie działania `pipe()` jako mechanizmu synchronizacji między procesami dziecka i opiekuna. Bez poprawnego pipe opiekun mógłby wykonać `msgrcv()` w złym momencie (zawieszenie lub błędne pobranie komunikatu).

**Przygotowanie (tryb testowy):**
- w `visitor.cpp` ustawienie na sztywno wieku < 8 lat (np. `wiek = 6`), aby **każdy** turysta tworzył opiekuna,
- pozostawienie normalnego tempa spawnowania, aby powstało dużo par procesów.

**Przebieg:**
1. Uruchomienie:
 ```bash
 cd build
 ./SymulacjaJaskini
 ```
2. Zatrzymanie `Ctrl+Z` i kontrola liczby procesów (weryfikacja „parowania”):
 ```bash
 ps -ef | grep Zwiedzajacy | grep -v grep | wc -l
 ps -ef | grep "Zwiedzajacy opiekun" | grep -v grep | wc -l
 ```
 Oczekiwane: liczby rosną stabilnie, brak „wiszących” opiekunów, procesy schodzą do zera po zakończeniu.
3. Wznowienie `fg` i obserwacja czy pary kończą pracę (procesy schodzą do `0` po zakończeniu).

**Weryfikacja po zakończeniu:**
```bash
ps aux | awk '$8 ~ /Z/' | wc -l
ipcs
grep -c "OPIEKUN" symulacja.log
```

**Kryteria zaliczenia:**
- brak zombie,
- brak pozostawionych zasobów IPC,
- w logu występują wpisy `OPIEKUN`, a procesy kończą się poprawnie (brak „zawieruszonych” opiekunów).

**Wynik:** ✅ PASS

---

### 5.5. Test zamykania symulacji (shutdown): użytkownik i strażnik + sprzątanie IPC

**Cel:** potwierdzenie, że zamknięcie symulacji nie powoduje:
- zawieszonych turystów na `msgrcv()` (brak odpowiedzi),
- pozostawionych procesów potomnych (Kasjer/Przewodnicy/Zwiedzający),
- procesów zombie,
- pozostawionych zasobów IPC (`ipcs` ma być puste).

**Scenariusz A - przedwczesne zamknięcie przez użytkownika (Ctrl+C):**
1. Start symulacji:
 ```bash
 cd build
 ./SymulacjaJaskini
 ```
2. W losowym momencie (gdy część osób jest w kolejce i część na trasie) wykonanie `Ctrl+C`.
3. Weryfikacja:
 ```bash
 pgrep -f "SymulacjaJaskini|Kasjer|Przewodnik|Straznik|Zwiedzajacy" | wc -l
 ps aux | awk '$8 ~ /Z/' | wc -l
 ipcs
 ```

**Scenariusz B - zamknięcie sterowane przez strażnika (SIGUSR1 do Strażnika):**
1. Start symulacji i wysłanie sygnału:
 ```bash
 pkill -USR1 Straznik
 ```
2. Oczekiwanie aż przewodnicy zablokują nowe wejścia, anulują oczekujących i doprowadzą do „opróżnienia jaskini”.
3. Weryfikacja końcowa:
 ```bash
 pgrep -f "SymulacjaJaskini|Kasjer|Przewodnik|Straznik|Zwiedzajacy" | wc -l
 ps aux | awk '$8 ~ /Z/' | wc -l
 ipcs
 ```

**Dodatkowa spójność:** osoby, które rozpoczęły zwiedzanie, kończą je (logi zawierają pary `Wejście na trasę` i `Zakończenie trasy`) i nie zostają „zawieruszone” procesy.

**Wynik:** ✅ PASS

---

### Podsumowanie testów
| Test | Co sprawdza | Wynik |
|------|-------------|-------|
| 5.1 | Integracyjnie: msgqueue + shm+sem + brak deadlock + brak zombie (5000 procesów) | ✅ PASS |
| 5.2 | Limit `K` na kładkach | ✅ PASS |
| 5.3 | Ruch jednokierunkowy na kładkach | ✅ PASS |
| 5.4 | Pipe: wymuszone pary dziecko+opiekun | ✅ PASS |
| 5.5 | Shutdown + sprzątanie IPC + brak „zawieszonych” procesów | ✅ PASS |



## 6. Linki do kodu źródłowego

*linki zostaną dodane na koniec, dla ukończonego projektu*

### 6.a. Tworzenie i obsługa plików
| Funkcja | Plik |
|---------|------|
| `creat()` | main.cpp |
| `open()` | common.hpp |
| `write()` | common.hpp |
| `close()` | common.hpp |
| `unlink()` | main.cpp |

### 6.b. Tworzenie procesów
| Funkcja | Plik |
|---------|------|
| `fork()` | main.cpp, visitor.cpp |
| `execl()` | main.cpp |
| `exit()` | main.cpp |
| `_exit()` | main.cpp |
| `waitpid()` | main.cpp |

### 6.c. Obsługa sygnałów
| Funkcja | Plik |
|---------|------|
| `signal()` | main.cpp, guide.cpp, guard.cpp |
| `kill()` | main.cpp, guard.cpp |

### 6.d. Synchronizacja procesów (semafory)
| Funkcja | Plik |
|---------|------|
| `ftok()` | main.cpp |
| `semget()` | main.cpp |
| `semctl()` | main.cpp |
| `semop()` | common.hpp |

### 6.e. Pamięć dzielona
| Funkcja | Plik |
|---------|------|
| `shmget()` | main.cpp |
| `shmat()` | main.cpp, cashier.cpp, guide.cpp |
| `shmdt()` | cashier.cpp, guide.cpp |
| `shmctl()` | main.cpp |

### 6.f. Kolejki komunikatów
| Funkcja | Plik |
|---------|------|
| `msgget()` | main.cpp |
| `msgsnd()` | visitor.cpp, cashier.cpp, guide.cpp |
| `msgrcv()` | cashier.cpp, visitor.cpp, guide.cpp |
| `msgctl()` | main.cpp |

### 6.g. Łącza nienazwane (pipe)
| Funkcja | Plik |
|---------|------|
| `pipe()` | visitor.cpp |
| `read()` | visitor.cpp |
| `write()` | visitor.cpp |

---

## 7. Podsumowanie

Projekt zrealizowany . Zastosowano:

**Wieloprocesowość:**
- `fork()`/`exec()` dla wszystkich procesów symulacji (kasjer, 2x przewodnik, strażnik, zwiedzający)
- Procesy działają asynchronicznie, komunikując się przez mechanizmy IPC

**Mechanizmy IPC:**
1. Pamięć dzielona - wspólny stan jaskini (kolejki, liczniki, flagi alarmów)
2. Semafor binarny - mutex chroniący dostęp do pamięci dzielonej
3. Kolejka komunikatów - komunikacja między procesami (żądania biletów, powiadomienia, zakończenia)
4. Sygnały SIGUSR1/SIGUSR2 - alarm zamknięcia od strażnika do przewodników
5. Pipe - synchronizacja grup dziecko+opiekun

**Synchronizacja:**
- Dwie niezależne kładki z trójstanowym kierunkiem ruchu (NONE, ENTERING, LEAVING)
- Limit K osób na kładce z blokowaniem przy próbie przekroczenia
- Resetowanie kierunku do NONE gdy kładka pusta

**Obsługa błędów:**
- `perror()` dla wszystkich funkcji systemowych
- Walidacja argumentów CLI z komunikatami błędów
- Graceful shutdown przy Ctrl+C z czyszczeniem zasobów IPC

**Główne wyzwania:**
- Synchronizacja dwóch kładek bez deadlocka (rozwiązano przez tablice stanów + resetowanie kierunku)
- Spójność grup 2-osobowych dziecko+opiekun (rozwiązano przez pipe)
- Graceful shutdown z powiadomieniem czekających w kolejce (rozwiązano przez komunikat anulowania z `trasa = -1`)

**Testy:**
- Przeprowadzono test obciążeniowy z 5000 procesami bez opóźnień - wszystkie mechanizmy IPC działały poprawnie
- Zweryfikowano synchronizację kładek, kolejkę komunikatów, pipe i sygnały
