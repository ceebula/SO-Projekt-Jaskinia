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
Zastosowano architekturę fork()/exec() z pięcioma typami procesów:
- **main.cpp** – kontroler: tworzy IPC, spawnuje procesy, sprząta zasoby
- **cashier.cpp** – kasjer: walidacja regulaminu, sprzedaż biletów, wstawianie do kolejki
- **guide.cpp** – przewodnik (x2): obsługa tras T1/T2, synchronizacja kładki
- **guard.cpp** – strażnik: wysyła sygnały zamknięcia przed godziną Tk
- **visitor.cpp** – zwiedzający: losowy wiek, zakup biletu, zwiedzanie

### 1.3. Mechanizmy IPC
| Mechanizm | Zastosowanie |
|-----------|--------------|
| Pamięć dzielona | Wspólny stan jaskini (kolejki, liczniki, flagi) |
| Semafor binarny | Mutex chroniący dostęp do pamięci dzielonej |
| Kolejka komunikatów | Komunikacja turysta↔kasjer, przewodnik→turysta |
| Sygnały SIGUSR1/2 | Alarm zamknięcia od strażnika do przewodników |
| Pipe (łącze nienazwane) | Synchronizacja dziecko↔opiekun |

### 1.4. Parametry symulacji
| Parametr | Wartość | Opis |
|----------|---------|------|
| N1, N2 | 10 | Max osób na trasie |
| K | 3 | Max osób na kładce jednocześnie |
| T1_MS | 2000 | Czas zwiedzania T1 [ms] |
| T2_MS | 3000 | Czas zwiedzania T2 [ms] |
| BRIDGE_DURATION_MS | 300 | Czas przejścia kładki [ms] |
| SECONDS_PER_HOUR | 6 | Sekund rzeczywistych na godzinę symulacji |

---

## 2. Ogólny opis kodu

### 2.1. Przepływ danych
```
Zwiedzający ──[msgsnd MSG_KASJER]──► Kasjer ──[q_push do shm]──► Kolejka w pamięci dzielonej
                                                                        │
Przewodnik ◄──[q_pop z kolejki]─────────────────────────────────────────┘
     │
     ├──[msgsnd MSG_ENTER_BASE+pid]──► Zwiedzający (powiadomienie o wejściu)
     │
     └──[msgrcv MSG_EXIT_T1/T2]◄────── Zwiedzający (zgłoszenie zakończenia)
```

### 2.2. Synchronizacja kładek
Każda trasa posiada własną kładkę z niezależną synchronizacją. Stan kładek przechowywany w tablicach:
- `osoby_na_kladce[2]` – liczba osób na każdej kładce
- `kierunek_ruchu_kladka[2]` – kierunek ruchu (enum: DIR_NONE, DIR_ENTERING, DIR_LEAVING)

Przewodnik trasy T*n* zarządza kładką `[n-1]`:
- Nowa osoba może wejść tylko gdy kierunek zgodny lub kładka pusta
- Po opróżnieniu kładki kierunek resetowany do DIR_NONE
- Limit K osób jednocześnie na każdej kładce
- Osobne logowanie do plików `kladka_t1.log` i `kladka_t2.log`

### 2.3. Grupy dziecko+opiekun
Dziecko <8 lat tworzy proces opiekuna przez `fork()` + `execl()`. Synchronizacja przez `pipe()`:
- Dziecko kupuje bilet dla grupy 2-osobowej
- Po otrzymaniu biletu sygnalizuje opiekunowi przez `write()` do pipe
- Opiekun czeka na `read()` i dopiero wtedy odbiera komunikat wejścia
- Oba procesy zwiedzają trasę równolegle

### 2.4. Kolejki priorytetowe
Powracający turyści (~10%) trafiają do osobnej kolejki `q_t*_prio`. Przewodnik najpierw sprawdza kolejkę priorytetową:
```cpp
if (stan->q_t1_prio.count > 0) { from_prio = true; return q_pop(stan->q_t1_prio, out); }
if (stan->q_t1.count > 0) return q_pop(stan->q_t1, out);
```

---

## 3. Co udało się zrobić
- Symulacja godzin pracy Tp–Tk (konfigurowalne przez `--open`, `--close`, `--spawn-ms`)
- Kładka jednokierunkowa z limitem K osób i zmiennym kierunkiem ruchu
- Limity pojemności tras (N1, N2) z blokowaniem nadmiarowych wejść
- Pełny regulamin wiekowy:
  - Dzieci <3 lat – bilet bezpłatny
  - Dzieci <8 lat – tylko T2, wymagany opiekun (grupa 2-osobowa)
  - Seniorzy 76+ – tylko T2
- Powroty (~10% turystów) ze zniżką 50% i kolejką priorytetową
- Sygnały SIGUSR1/SIGUSR2 wysyłane przez strażnika przed godziną zamknięcia
- Anulowanie oczekujących w kolejce po alarmie (powiadomienie przez komunikat)
- Logging zdarzeń do plików: `symulacja.log`, `kladka_t1.log`, `kladka_t2.log`
- Podsumowanie finansowe na końcu symulacji
- Walidacja argumentów CLI z komunikatami błędów
- Kolorowanie wyjścia terminala kodami ANSI


---

## 4. Napotkane problemy

### 4.1. Wyścigi przy dostępie do pamięci dzielonej
**Problem:** Wiele procesów (kasjer, przewodnicy, główny) modyfikuje wspólne liczniki i kolejki. Bez synchronizacji mogło dojść do niespójności (np. `osoby_na_kladce` ujemne, zdublowane wpisy w kolejce).

**Rozwiązanie:** Semafor binarny jako mutex. Każda operacja na `JaskiniaStan` otoczona `lock_sem()`/`unlock_sem()`. Wersja `lock_sem_interruptible()` dla obsługi sygnałów.

### 4.2. Synchronizacja jednokierunkowych kładek
**Problem:** Deadlock gdy grupa wchodząca i wychodząca jednocześnie próbują zająć kładkę. Klasyczny problem readers-writers ale z dwoma kierunkami. Dodatkowo dwie niezależne kładki dla dwóch tras.

**Rozwiązanie:** Tablice `osoby_na_kladce[2]` i `kierunek_ruchu_kladka[2]` z trzema stanami kierunku:
- `DIR_NONE` – kładka wolna, można wejść w dowolnym kierunku
- `DIR_ENTERING` – tylko wejścia dozwolone
- `DIR_LEAVING` – tylko wyjścia dozwolone

Każdy przewodnik operuje na swojej kładce przez lokalne wskaźniki:
```cpp
int* kladka = &stan->osoby_na_kladce[trasa-1];
int* kierunek = &stan->kierunek_ruchu_kladka[trasa-1];
```

Nowa osoba może wejść tylko gdy `*kierunek == zgodny || *kierunek == DIR_NONE`. Po opróżnieniu → reset do DIR_NONE. Logowanie w formacie `przed->po` dla łatwej weryfikacji.

### 4.3. Alarm: rozróżnienie "przed turą" vs "w trakcie"
**Problem:** Sygnał od strażnika musi:
- Blokować nowe wejścia (nie wpuszczać kolejnych grup)
- Pozwolić grupom na trasie dokończyć zwiedzanie
- Anulować oczekujących bez ich zawieszania

**Rozwiązanie:** 
- Flaga `alarm_t1`/`alarm_t2` w pamięci dzielonej (ustawiana w handlerze sygnału)
- Kasjer sprawdza flagę i odmawia nowych biletów
- Przewodnik przy alarmie i pustej trasie wywołuje `cancel_waiting_groups_locked()` → wysyła `trasa = -1` do każdego czekającego
- Turysta sprawdza `msgEnter.trasa == -1` i kończy proces

### 4.4. Powiadomienie czekających przy anulowaniu
**Problem:** Turyści czekający na `msgrcv()` (blokujące) musieliby się zawiesić na zawsze, gdyby kolejka została anulowana bez powiadomienia.

**Rozwiązanie:** Przy anulowaniu przewodnik wysyła do każdego PID-a z kolejki komunikat z `trasa = -1`:
```cpp
static void notify_cancel(pid_t pid) {
    MsgEnter msgCancel{};
    msgCancel.mtype = MSG_ENTER_BASE + pid;
    msgCancel.trasa = -1;  // sygnał anulowania
    msgsnd(msg_id, &msgCancel, sizeof(msgCancel) - sizeof(long), IPC_NOWAIT);
}
```

### 4.5. Grupa dziecko+opiekun jako spójna jednostka
**Problem:** Dwa procesy (dziecko i opiekun) muszą:
- Kupić jeden bilet dla grupy 2
- Wejść razem na kładkę (liczyć jako 2 osoby)
- Otrzymać osobne powiadomienia o wejściu

**Rozwiązanie:**
- `GroupItem` przechowuje `group_size` i tablicę `pids[2]`
- Dziecko tworzy opiekuna przez `fork()` + `execl("./Zwiedzajacy", "opiekun", ...)`
- Synchronizacja przez `pipe()` – opiekun czeka na `read()` zanim połączy się z kolejką
- Przewodnik wysyła `MsgEnter` do obu PID-ów osobno

### 4.6. Zachowanie priorytetu przy requeue
**Problem:** Gdy grupa została pobrana z kolejki priorytetowej, ale nie może wejść (pełna kładka/trasa), musi wrócić do kolejki z zachowaniem priorytetu.

**Rozwiązanie:** Flaga `from_prio` przy `dequeue_group_locked()`. Jeśli grupa nie może wejść, wraca do odpowiedniej kolejki:
```cpp
if (from_prio) q_push(stan->q_t1_prio, it);
else q_push(stan->q_t1, it);
```

### 4.7. Sprzątanie IPC przy przerwaniu
**Problem:** Ctrl+C lub błąd mogą pozostawić zasoby IPC w systemie (widoczne w `ipcs`).

**Rozwiązanie:** Handler `SIGINT` w main.cpp:
1. `kill(0, SIGTERM)` – sygnał do całej grupy procesów
2. Pętla `waitpid(-1, WNOHANG)` – zbieranie zombie
3. `shmctl(IPC_RMID)`, `semctl(IPC_RMID)`, `msgctl(IPC_RMID)` – usuwanie IPC
4. `unlink(FTOK_FILE)` – usunięcie pliku klucza

---

## 5. Testy

### 5.1 Testy automatyczne
Skrypt: `tests/run_tests.sh`  
Wyniki: `tests/wyniki_testow.txt`

| Test | Opis | Wynik |
|------|------|-------|
| TEST 1 | Normalny ruch (spawn co 500ms, 12s) | ✅ PASS |
| TEST 2 | Stress test (spawn co 5ms, 10s) | ✅ PASS |
| TEST 3 | Weryfikacja regulaminu wiekowego (spawn co 200ms, 15s) | ✅ PASS |

**Sprawdzane aspekty:**
- ✅ Brak procesów zombie po zakończeniu
- ✅ Brak osieroconych procesów
- ✅ Usunięcie pliku `ftok.key`
- ✅ Zwolnienie zasobów IPC (shm, sem, msg)
- ✅ Limit kładki K=3 zachowany
- ✅ Regulamin wiekowy (dzieci <8 tylko T2)

### 5.2 Testy manualne

#### Test: Przepustowość kładek (limit K)
**Scenariusz:** `--spawn-ms 5` (wysoki ruch, 40 sekund)  
**Obserwacja:** Logi pokazują `kladka=X->Y` gdzie X,Y <= 3 (osobne dla każdej trasy)  
**Wynik:** ✅ Limit K=3 nigdy nieprzekroczony na żadnej kładce

#### Test: Synchronizacja ruchu jednokierunkowego
**Scenariusz:** Obserwacja logów `kladka_t1.log` i `kladka_t2.log`  
**Obserwacja:** Brak kolizji kierunków – każda kładka niezależna. Format `kierunek=NONE->IN` lub `IN->NONE`  
**Wynik:** ✅ Ruch tylko w jednym kierunku na każdej kładce

#### Test: Logika biznesowa
**Scenariusz:** Generowanie losowych turystów  
**Obserwacja w logach:**
- Dzieci <8 tylko T2 z grupą=2
- Seniorzy 76+ tylko T2
- Powroty z tagiem `(powrot -50%)`  
**Wynik:** ✅ Regulamin przestrzegany

#### Test: Graceful shutdown
**Scenariusz:** Ctrl+C w trakcie + `ipcs` po zakończeniu  
**Obserwacja:** Brak pozostałych semaforów/shm/msg  
**Wynik:** ✅ Zasoby IPC wyczyszczone, 0 zombie

#### Test: Pause/Resume
**Scenariusz:** Ctrl+Z podczas symulacji, potem `fg`  
**Obserwacja:** Symulacja zatrzymuje się i wznawia poprawnie  
**Wynik:** ✅ SIGTSTP/SIGCONT obsługiwane poprawnie

---

## 6. Linki do kodu źródłowego


### 6.a. Tworzenie i obsługa plików
| Funkcja | Plik | Linia |
|---------|------|-------|
| `creat()` | main.cpp | |
| `open()` | common.hpp | |
| `write()` | common.hpp | |
| `close()` | common.hpp | |
| `unlink()` | main.cpp | |

### 6.b. Tworzenie procesów
| Funkcja | Plik | Linia |
|---------|------|-------|
| `fork()` | main.cpp | |
| `execl()` | main.cpp | |
| `exit()` | main.cpp | |
| `_exit()` | main.cpp | |
| `waitpid()` | main.cpp | |
| `fork()` (opiekun) | visitor.cpp | |

### 6.c. Obsługa sygnałów
| Funkcja | Plik | Linia |
|---------|------|-------|
| `signal()` | main.cpp | |
| `signal()` (przewodnik) | guide.cpp | |
| `signal()` (strażnik) | guard.cpp | |
| `kill()` (SIGTERM) | main.cpp | |
| `kill()` (SIGUSR1) | guard.cpp | |
| `kill()` (SIGUSR2) | guard.cpp | |

### 6.d. Synchronizacja procesów (semafory)
| Funkcja | Plik | Linia |
|---------|------|-------|
| `ftok()` | main.cpp | |
| `semget()` | main.cpp | |
| `semctl()` (SETVAL) | main.cpp | |
| `semctl()` (IPC_RMID) | main.cpp | |
| `semop()` (P) | common.hpp | |
| `semop()` (V) | common.hpp | |

### 6.e. Pamięć dzielona
| Funkcja | Plik | Linia |
|---------|------|-------|
| `shmget()` | main.cpp | |
| `shmat()` | main.cpp | |
| `shmdt()` | cashier.cpp | |
| `shmctl()` (IPC_RMID) | main.cpp | |

### 6.f. Kolejki komunikatów
| Funkcja | Plik | Linia |
|---------|------|-------|
| `msgget()` | main.cpp | |
| `msgsnd()` | visitor.cpp | |
| `msgrcv()` | cashier.cpp | |
| `msgctl()` (IPC_RMID) | main.cpp | |

### 6.g. Łącza nienazwane (pipe)
| Funkcja | Plik | Linia |
|---------|------|-------|
| `pipe()` | visitor.cpp | |
| `read()` | visitor.cpp | |
| `write()` | visitor.cpp | |

---

## 7. Podsumowanie

Projekt zrealizowany zgodnie z wymaganiami. Zastosowano:
- **Wieloprocesowość:** fork()/exec() dla wszystkich aktorów symulacji
- **4 mechanizmy IPC:** pamięć dzielona, semafory, kolejki komunikatów, sygnały + pipe
- **Synchronizację:** semafor binarny jako mutex, dwie niezależne kładki z kierunkiem ruchu
- **Obsługę błędów:** perror() dla wszystkich funkcji systemowych, walidacja CLI

Główne wyzwania: synchronizacja dwóch kładek bez deadlocka, spójność grup 2-osobowych, graceful shutdown z powiadomieniem czekających.
