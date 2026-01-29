#!/bin/bash
# Automatyczne testy symulacji jaskini
# Uruchomienie: ./tests/run_tests.sh

cd "$(dirname "$0")/.."

BUILD_DIR="build"
LOG_FILE="$BUILD_DIR/symulacja.log"
KLADKA_T1_LOG="$BUILD_DIR/kladka_t1.log"
KLADKA_T2_LOG="$BUILD_DIR/kladka_t2.log"
RESULTS_FILE="tests/wyniki_testow.txt"

# Kolory
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

passed=0
failed=0

log_result() {
    echo "$1" >> "$RESULTS_FILE"
}

run_test() {
    local name="$1"
    local spawn_ms="$2"
    local duration="$3"
    local description="$4"
    
    echo -e "${YELLOW}[$name]${NC} $description"
    log_result "=== $name ==="
    log_result "Opis: $description"
    log_result "Parametry: --spawn-ms $spawn_ms, czas: ${duration}s"
    
    # Cleanup przed testem
    ipcrm -a 2>/dev/null || true
    rm -f "$BUILD_DIR/ftok.key" "$LOG_FILE" "$KLADKA_T1_LOG" "$KLADKA_T2_LOG" 2>/dev/null || true
    
    # Uruchomienie symulacji
    cd "$BUILD_DIR"
    timeout "$duration" ./SymulacjaJaskini --spawn-ms "$spawn_ms" > /dev/null 2>&1 || true
    cd ..
    
    sleep 1
}

check_no_zombies() {
    local zombies=$(ps aux | awk '$8 ~ /Z/' | grep -E "(Jaskini|Przewodnik|Kasjer|Zwiedzaj|Straznik)" | wc -l)
    if [ "$zombies" -eq 0 ]; then
        echo -e "  ${GREEN}✓${NC} Brak procesów zombie"
        log_result "  [PASS] Brak procesów zombie"
        return 0
    else
        echo -e "  ${RED}✗${NC} Znaleziono $zombies zombie!"
        log_result "  [FAIL] Znaleziono $zombies zombie"
        return 1
    fi
}

check_no_orphans() {
    local orphans=$(ps aux | grep -E "(Jaskini|Przewodnik|Kasjer|Zwiedzaj|Straznik)" | grep -v grep | wc -l)
    if [ "$orphans" -eq 0 ]; then
        echo -e "  ${GREEN}✓${NC} Brak osieroconych procesów"
        log_result "  [PASS] Brak osieroconych procesów"
        return 0
    else
        echo -e "  ${RED}✗${NC} Znaleziono $orphans osieroconych procesów!"
        log_result "  [FAIL] Znaleziono $orphans osieroconych procesów"
        return 1
    fi
}

check_ipc_cleanup() {
    if [ ! -f "$BUILD_DIR/ftok.key" ]; then
        echo -e "  ${GREEN}✓${NC} Plik ftok.key usunięty"
        log_result "  [PASS] Plik ftok.key usunięty"
        return 0
    else
        echo -e "  ${RED}✗${NC} Plik ftok.key pozostał!"
        log_result "  [FAIL] Plik ftok.key pozostał"
        return 1
    fi
}

check_log_exists() {
    if [ -f "$LOG_FILE" ] && [ -s "$LOG_FILE" ]; then
        local lines=$(wc -l < "$LOG_FILE")
        echo -e "  ${GREEN}✓${NC} Plik logu utworzony ($lines linii)"
        log_result "  [PASS] Plik logu: $lines linii"
        return 0
    else
        echo -e "  ${RED}✗${NC} Brak pliku logu!"
        log_result "  [FAIL] Brak pliku logu"
        return 1
    fi
}

check_bridge_limit() {
    if [ ! -f "$LOG_FILE" ]; then
        echo -e "  ${YELLOW}?${NC} Brak logu do analizy"
        log_result "  [SKIP] Brak logu"
        return 0
    fi
    
    local max_bridge=$(grep -oP 'kladka=\K[0-9]+' "$LOG_FILE" 2>/dev/null | sort -n | tail -1 || echo 0)
    if [ -z "$max_bridge" ]; then max_bridge=0; fi
    
    if [ "$max_bridge" -le 3 ]; then
        echo -e "  ${GREEN}✓${NC} Limit kładki K=3 zachowany (max: $max_bridge)"
        log_result "  [PASS] Limit kładki: max $max_bridge <= 3"
        return 0
    else
        echo -e "  ${RED}✗${NC} Przekroczono limit kładki! (max: $max_bridge)"
        log_result "  [FAIL] Limit kładki przekroczony: $max_bridge > 3"
        return 1
    fi
}

check_regulations() {
    if [ ! -f "$LOG_FILE" ]; then
        return 0
    fi
    
    local child_t1=$(grep -E "wiek=[1-7][^0-9].*T1" "$LOG_FILE" 2>/dev/null | grep -v "T2" | wc -l || echo 0)
    if [ "$child_t1" -eq 0 ]; then
        echo -e "  ${GREEN}✓${NC} Dzieci <8 tylko na T2"
        log_result "  [PASS] Regulamin: dzieci <8 tylko T2"
        return 0
    else
        echo -e "  ${RED}✗${NC} Znaleziono dzieci <8 na T1!"
        log_result "  [FAIL] Dzieci <8 na T1"
        return 1
    fi
}

check_one_direction() {
    # Sprawdź osobne logi kładek - format: czas AKCJA kladka=przed->po kierunek=przed->po
    # Kolizja = przejście IN->OUT lub OUT->IN (nie powinno się zdarzyć)
    
    local collision=0
    for logfile in "$KLADKA_T1_LOG" "$KLADKA_T2_LOG"; do
        if [ -f "$logfile" ]; then
            # Szukaj kolizji: kierunek zmienia się z IN na OUT lub OUT na IN
            if grep -E "kierunek=(IN->OUT|OUT->IN)" "$logfile" > /dev/null 2>&1; then
                echo -e "  ${RED}✗${NC} Kolizja kierunków na kładce $(basename $logfile)!"
                log_result "  [FAIL] Kolizja kierunków w $(basename $logfile)"
                collision=1
            fi
        fi
    done
    
    if [ "$collision" -eq 0 ]; then
        echo -e "  ${GREEN}✓${NC} Ruch jednokierunkowy (przejścia przez NONE)"
        log_result "  [PASS] Ruch jednokierunkowy - brak kolizji"
        return 0
    fi
    return 1
}

evaluate_test() {
    local test_passed=true
    
    check_no_zombies || test_passed=false
    check_no_orphans || test_passed=false
    check_ipc_cleanup || test_passed=false
    check_log_exists || test_passed=false
    
    if $test_passed; then
        ((passed++))
        echo -e "  ${GREEN}PASSED${NC}"
        log_result "  WYNIK: PASSED"
    else
        ((failed++))
        echo -e "  ${RED}FAILED${NC}"
        log_result "  WYNIK: FAILED"
    fi
    log_result ""
    echo ""
}

# ============ GŁÓWNA CZĘŚĆ ============

echo "========================================"
echo "  TESTY AUTOMATYCZNE - SYMULACJA JASKINI"
echo "========================================"
echo ""

# Kompilacja
echo -e "${YELLOW}Kompilacja...${NC}"
cd "$BUILD_DIR" && make -j4 > /dev/null 2>&1 && cd ..
echo -e "${GREEN}OK${NC}"
echo ""

# Inicjalizacja pliku wyników
echo "WYNIKI TESTÓW AUTOMATYCZNYCH" > "$RESULTS_FILE"
echo "========================================" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

# TEST 1: Normalny ruch
run_test "TEST 1" 500 12 "Normalny ruch (spawn co 500ms, 12s)"
evaluate_test

# TEST 2: Stress test
run_test "TEST 2" 5 10 "Stress test (spawn co 5ms, 10s)"
check_bridge_limit
check_one_direction
evaluate_test

# TEST 3: Sprawdzenie regulaminu
run_test "TEST 3" 200 15 "Weryfikacja regulaminu wiekowego (spawn co 200ms, 15s)"
check_regulations
evaluate_test

# ============ PODSUMOWANIE ============

echo "========================================"
echo "  PODSUMOWANIE"
echo "========================================"
echo -e "  Passed: ${GREEN}$passed${NC}"
echo -e "  Failed: ${RED}$failed${NC}"
echo ""

log_result "========================================"
log_result "PODSUMOWANIE"
log_result "  Passed: $passed"
log_result "  Failed: $failed"
log_result "========================================"
