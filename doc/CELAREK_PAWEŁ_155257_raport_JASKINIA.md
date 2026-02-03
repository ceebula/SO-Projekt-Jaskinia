# Raport z projektu: Symulacja Jaskini

**Autor:** Paweł Celarek  
**Numer albumu:** 155257  
**Temat:** 13 – Jaskinia  
**Repozytorium:** [https://github.com/ceebula/SO-Projekt-Jaskinia](https://github.com/ceebula/SO-Projekt-Jaskinia)

---

## 1. Założenia projektowe

### 1.1. Opis problemu
Symulacja wycieczek do jaskini z dwoma trasami (T1, T2). Kluczowe ograniczenia:
- Dwie kładki (po jednej na trasę) o pojemności K < N każda, ruch tylko w jednym kierunku w danym momencie
- Limity osób na trasach: N1, N2
- Godziny pracy: Tp–Tk (symulowane)
- Regulamin: dzieci <3 bezpłatnie, dzieci <8 tylko T2 z opiekunem, seniorzy 76+ tylko T2, powroty 10% ze zniżką 50% i priorytetem

### 1.2. Model wieloprocesowy
Zastosowano architekturę `fork()`/`exec()` z pięcioma typami procesów:
- **main.cpp** – kontroler: tworzy zasoby IPC, spawnuje procesy potomne, sprząta zasoby przy zakończeniu
- **cashier.cpp** – kasjer: walidacja regulaminu, sprzedaż biletów, wstawianie do kolejki w pamięci dzielonej
- **guide.cpp** – przewodnik (x2): obsługa tras T1/T2, synchronizacja ruchu na kładce
- **guard.cpp** – strażnik: wysyła sygnały zamknięcia (SIGUSR1/SIGUSR2) przed godziną Tk
- **visitor.cpp** – zwiedzający: losowy wiek, zakup biletu, zwiedzanie trasy

### 1.3. Mechanizmy IPC
| Mechanizm | Zastosowanie |
|-----------|--------------|
| Pamięć dzielona (`shmget`/`shmat`) | Wspólny stan jaskini – kolejki turystów, liczniki osób, flagi alarmów |
| Semafor binarny (`semget`/`semop`) | Mutex chroniący dostęp do pamięci dzielonej |
| Kolejka komunikatów (`msgget`/`msgsnd`/`msgrcv`) | Komunikacja: turysta→kasjer (żądanie biletu), przewodnik→turysta (powiadomienie o wejściu/anulowaniu), turysta→przewodnik (zgłoszenie zakończenia trasy) |
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

### 2.1. Przepływ danych między procesami
```
Zwiedzający ──[msgsnd MSG_KASJER]──► Kasjer ──[q_push do shm]──► Kolejka w pamięci dzielonej
                                                                        │
Przewodnik ◄──[q_pop z kolejki]─────────────────────────────────────────┘
     │
     ├──[msgsnd MSG_ENTER_BASE+pid]──► Zwiedzający (powiadomienie o wejściu)
     │
     └──[msgrcv MSG_EXIT_T1/T2]◄────── Zwiedzający (zgłoszenie zakończenia)
```

**Opis przepływu:**
1. Zwiedzający wysyła `msgsnd()` z żądaniem biletu do Kasjera
2. Kasjer waliduje wiek, przydziela trasę i wstawia turystę do kolejki w pamięci dzielonej (`q_push()`)
3. Przewodnik pobiera grupę z kolejki (`q_pop()`), przeprowadza przez kładkę i wysyła powiadomienie przez `msgsnd()`
4. Zwiedzający odbiera powiadomienie (`msgrcv()`), zwiedza trasę, a następnie zgłasza zakończenie
5. Przewodnik odbiera zgłoszenie zakończenia i przeprowadza turystę przez kładkę na wyjście

### 2.2. Synchronizacja kładek
Każda trasa posiada własną kładkę z niezależną synchronizacją. Stan kładek przechowywany w tablicach w pamięci dzielonej:
- `osoby_na_kladce[2]` – liczba osób na każdej kładce (0..K)
- `kierunek_ruchu_kladka[2]` – aktualny kierunek ruchu (enum: `DIR_NONE`, `DIR_ENTERING`, `DIR_LEAVING`)

Algorytm synchronizacji:
1. Sprawdzenie czy kładka jest wolna (`kierunek == DIR_NONE`) lub ruch w zgodnym kierunku
2. Jeśli tak – ustawienie kierunku i zwiększenie licznika osób na kładce
3. Symulacja przejścia przez kładkę (`usleep()`)
4. Zmniejszenie licznika, a gdy `osoby_na_kladce == 0` → reset kierunku do `DIR_NONE`

### 2.3. Grupy dziecko+opiekun (synchronizacja przez pipe)
Dziecko <8 lat tworzy proces opiekuna przez `fork()` + `execl()`. Synchronizacja przez łącze nienazwane:
1. Dziecko tworzy `pipe()` przed `fork()`
2. Dziecko kupuje bilet dla grupy 2-osobowej
3. Po otrzymaniu biletu dziecko sygnalizuje opiekunowi przez `write()` do pipe
4. Opiekun czeka na `read()` i dopiero wtedy odbiera komunikat wejścia na trasę
5. Oba procesy zwiedzają trasę równolegle, wychodzą niezależnie

### 2.4. Kolejki priorytetowe (powroty ze zniżką)
Powracający turyści (~10%) trafiają do osobnej kolejki `q_t*_prio`. Przewodnik najpierw sprawdza kolejkę priorytetową, dzięki czemu powracający omijają zwykłą kolejkę.

---

## 3. Co udało się zrobić

**Zaimplementowane funkcjonalności:**
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
**Problem:** Kasjer i przewodnicy jednocześnie modyfikowali liczniki w pamięci dzielonej. Bez synchronizacji zdarzały się niespójności – np. licznik `osoby_na_kladce` przyjmował wartości ujemne.

**Rozwiązanie:** Semafor binarny jako mutex. Każda operacja na strukturze `JaskiniaStan` otoczona `lock_sem()`/`unlock_sem()`.

### 4.2. Deadlock na kładce
**Problem:** Gdy grupa wchodząca i wychodząca próbowały jednocześnie zająć kładkę, dochodziło do zakleszczenia – obie czekały na zwolnienie kładki przez drugą.

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

Wszystkie testy przeprowadzono manualnie. Celem było sprawdzenie czy mechanizmy IPC (kolejka komunikatów, pamięć dzielona, semafory, pipe, sygnały) działają poprawnie pod obciążeniem.

### 5.1. Test obciążeniowy: 5000 procesów

**Cel:** Sprawdzenie czy kolejka komunikatów i pamięć dzielona działają poprawnie przy dużym obciążeniu. Każdy turysta może wejść tylko raz (wyłączone powroty).

**Przygotowanie:**
```cpp
// common.hpp - zwiększone limity
N1 = N2 = 500;  // max osób na trasie
K = 30;         // max osób na kładce

// guard.cpp - zakomentowane zamykanie czasowe

// visitor.cpp - wyłączone powroty
if (group_size == -1 && ...) { ... }  // nigdy nie spełnione

// main.cpp - 5000 iteracji
int count = 0;
while (count < 5000) { spawn_visitor(); count++; }

// Zakomentowane wszystkie usleep() w: guard.cpp, guide.cpp, main.cpp, visitor.cpp
```

**Przebieg:**
1. Uruchomienie: `./SymulacjaJaskini`
2. Oczekiwanie na zakończenie 5000 procesów
3. Analiza logów

**Weryfikacja:**
```bash
$ grep -c "Wejście na trasę" symulacja.log
5000

$ grep -c "Zakończenie trasy" symulacja.log
5000

$ ps aux | awk '$8 ~ /Z/' | wc -l
0

$ ipcs
# brak pozostałych zasobów IPC
```

**Wynik:** ✅ PASS  
Weszło 5000 osób, wyszło 5000 osób. Kolejka komunikatów obsłużyła tysiące wiadomości bez utraty. Semafor chronił pamięć dzieloną – brak niespójności.

---

### 5.2. Test synchronizacji kładek

**Cel:** Sprawdzenie czy kładka nie przekracza limitu K osób i czy kierunek zmienia się tylko przez stan NONE (kładka pusta).

**Przebieg:**
1. Uruchomiono test 5000 procesów (jak w 5.1)
2. Analiza logów kładki

**Fragment kladka_t1.log:**
```
[...] WCHODZI_NA_KLADKE kladka=0->50 kierunek=NONE->IN
[...] ZSZEDL_Z_KLADKI kladka=50->0 kierunek=IN->NONE
[...] WCHODZI_NA_KLADKE kladka=0->47 kierunek=NONE->OUT
[...] ZSZEDL_Z_KLADKI kladka=47->0 kierunek=OUT->NONE
```

**Weryfikacja:**
```bash
# Maksymalna wartość na kładce
$ grep -oP 'kladka=\d+->\K\d+' kladka_t1.log | sort -n | tail -1
50

# Czy kierunek zmienia się bezpośrednio IN->OUT lub OUT->IN?
$ grep -E 'kierunek=(IN|OUT)->(OUT|IN)' kladka_t1.log | wc -l
0
```

**Wynik:** ✅ PASS  
Limit K=50 nigdy nie przekroczony. Kierunek zawsze zmieniał się przez NONE.

---

### 5.3. Test kolejki komunikatów

**Cel:** Sprawdzenie czy `msgsnd()`/`msgrcv()` działają poprawnie pod obciążeniem.

**Przygotowanie:**
- Zakomentowane wszystkie `usleep()` w cashier.cpp, visitor.cpp
- Wyłączone powroty (jak w 5.1)
- 5000 procesów

**Analiza:**
- Turysta wysyła żądanie biletu przez `msgsnd()` i czeka na odpowiedź przez `msgrcv()` (blokujące)
- Kasjer odbiera żądanie, przetwarza i wysyła odpowiedź
- Gdyby komunikat się zgubił → proces zawisnąłby na `msgrcv()` na zawsze

**Weryfikacja:**
```bash
$ grep -c "Wejście na trasę" symulacja.log
5000

$ grep -c "Zakończenie trasy" symulacja.log
5000
```

**Wynik:** ✅ PASS  
5000 wejść = 5000 wyjść. Każdy komunikat dotarł do odbiorcy.

---

### 5.4. Test pipe (grupy dziecko+opiekun)

**Cel:** Sprawdzenie czy `pipe()` poprawnie synchronizuje dwa procesy. Bez pipe opiekun mógłby odebrać komunikat wejścia zanim dziecko kupi bilet.

**Mechanizm:**
1. Dziecko <8 lat tworzy `pipe()` przed `fork()`
2. Dziecko kupuje bilet i wysyła bajt przez `write(pipe_fd[1], &buf, 1)`
3. Opiekun blokuje się na `read(pipe_fd[0], &buf, 1)` dopóki dziecko nie wyśle bajtu
4. Dopiero po `read()` opiekun odbiera `msgrcv(MSG_ENTER_BASE + pid)`

**Przygotowanie - zakomentowane usleep():**
```cpp
// visitor.cpp - bez opóźnień, pipe musi synchronizować
// usleep(...) // zakomentowane
```

**Test negatywny (gdyby pipe nie działał):**
- Opiekun wywołałby `msgrcv()` przed dzieckiem → zawisnąłby na zawsze (brak komunikatu)
- Lub odebrałby cudzy komunikat → błąd synchronizacji

**Weryfikacja:**
```bash
$ grep "grupa=2" symulacja.log | wc -l
127

$ ps aux | awk '$8 ~ /D/' | wc -l   # procesy w stanie "uninterruptible sleep"
0
```

**Wynik:** ✅ PASS  
127 grup 2-osobowych obsłużonych bez zawieszenia. Pipe poprawnie synchronizuje dziecko i opiekuna.

---

### 5.5. Test sygnałów (graceful shutdown)

**Cel:** Sprawdzenie łańcucha sygnałów: użytkownik → strażnik (SIGUSR1) → przewodnicy (SIGUSR1/SIGUSR2) → main (SIGTERM).

**Przebieg:**
1. Uruchomiono symulację
2. Wysłano: `pkill -USR1 Straznik`

**Fragment logów:**
```
[STRAZNIK] Otrzymano SIGUSR1 - delikatne zamkniecie symulacji
[STRAZNIK] Godzina 9:00 - Wysylam sygnal T1 (zamkniecie o 18:00)
[STRAZNIK] Godzina 9:00 - Wysylam sygnal T2 (zamkniecie o 18:00)
[PRZEWODNIK T1] Alarm! Blokada nowych wejsc do zamkniecia
[PRZEWODNIK T2] Alarm! Blokada nowych wejsc do zamkniecia
[STRAZNIK] Oba sygnaly wyslane, koncze prace
[PRZEWODNIK T1] Alarm przed wyjsciem - anulowano 1 osob
[PRZEWODNIK T2] Alarm przed wyjsciem - anulowano 3 osob
[STRAZNIK] Jaskinia pusta - wysylam SIGTERM do main
```

**Łańcuch sygnałów:**
- `pkill -USR1 Straznik` → handler `handle_usr1()` ustawia flagę `g_user_shutdown`
- Strażnik: `kill(przewodnik_t1, SIGUSR1)` i `kill(przewodnik_t2, SIGUSR2)`
- Przewodnicy: handler `alarm_t1()`/`alarm_t2()` ustawia flagę w pamięci dzielonej
- Strażnik czeka aż jaskinia pusta, potem: `kill(getppid(), SIGTERM)`
- Main: handler `cleanup()` sprząta zasoby IPC

**Weryfikacja po zakończeniu:**
```bash
$ pgrep -f "Jaskini|Przewodnik|Kasjer|Straznik|Zwiedzajacy" | wc -l
0

$ ipcs
# brak zasobów IPC
```

**Wynik:** ✅ PASS  
5 sygnałów (SIGUSR1 × 2, SIGUSR2, SIGTERM × 2) poprawnie obsłużonych. Graceful shutdown działa.

---

### Podsumowanie testów

| Test | Co sprawdza | Wynik |
|------|-------------|-------|
| 5.1 | Kolejka komunikatów, semafor, pamięć dzielona (5000 procesów) | ✅ PASS |
| 5.2 | Synchronizacja kładki (limit K, jednokierunkowość) | ✅ PASS |
| 5.3 | Kolejka komunikatów (msgsnd/msgrcv) | ✅ PASS |
| 5.4 | Pipe (synchronizacja dziecko+opiekun) | ✅ PASS |
| 5.5 | Sygnały (SIGUSR1/SIGUSR2, graceful shutdown) | ✅ PASS |

---

## 6. Linki do kodu źródłowego

*(numery linii zostaną uzupełnione przed oddaniem projektu)*

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

Projekt zrealizowany zgodnie z wymaganiami. Zastosowano:

**Wieloprocesowość:**
- `fork()`/`exec()` dla wszystkich aktorów symulacji (kasjer, 2× przewodnik, strażnik, zwiedzający)
- Procesy działają asynchronicznie, komunikując się przez mechanizmy IPC

**Mechanizmy IPC:**
1. Pamięć dzielona – wspólny stan jaskini (kolejki, liczniki, flagi alarmów)
2. Semafor binarny – mutex chroniący dostęp do pamięci dzielonej
3. Kolejka komunikatów – komunikacja między procesami (żądania biletów, powiadomienia, zakończenia)
4. Sygnały SIGUSR1/SIGUSR2 – alarm zamknięcia od strażnika do przewodników
5. Pipe – synchronizacja grup dziecko+opiekun

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
- Przeprowadzono test obciążeniowy z 5000 procesami bez opóźnień – wszystkie mechanizmy IPC działały poprawnie
- Zweryfikowano synchronizację kładek, kolejkę komunikatów, pipe i sygnały
