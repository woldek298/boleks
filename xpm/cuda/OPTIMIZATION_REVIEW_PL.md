# Analiza i propozycje usprawnień (`sha256.cu`, `fermat.cu`)

## 1) Najważniejsze ryzyka poprawności

1. **`sum24()` może czytać 64 bity z niegwarantowanym wyrównaniem** przez rzutowanie `uint32_t*` na `uint64_t*`.
   - To jest potencjalnie niebezpieczne (UB po stronie C++ i możliwe wolniejsze loady na GPU).
   - Lepszy wariant: ładować dwa `uint32_t` i skleić lokalnie do `uint64_t`.

2. W `sha256.cu` jest komentarz, że dla dzielników **53 i 67** „24-bit arithmetic ... can produce wrong results”.
   - To oznacza realne ryzyko fałszywie dodatnich/ujemnych trafień.
   - Najbezpieczniej dla tych dwóch przypadków przejść na pełne 32-bitowe sprawdzenie modulo (lub dodatkową walidację tylko dla kandydatów).

3. `FermatTest352()` i `FermatTest320()` zmniejszają licznik przez `remaining -= windowSize`, mimo że realnie przetwarzają `size = min(remaining, windowSize)`.
   - Działa „przypadkiem” dla zakończenia pętli, ale jest mylące i utrudnia audyt.
   - Czytelniej i bezpieczniej: `remaining -= size`.

## 2) Wydajność CUDA (największy potencjał)

1. **Presja rejestrowa i możliwe spilly**
   - `sha256()` trzyma `w[64]` + 8 rejestrów stanu + tymczasowe.
   - `sha256UsePrecalc()` dubluje prawie ten sam kod.
   - Warto profilować (`nvcc --ptxas-options=-v`, Nsight Compute) i rozważyć:
     - generowanie części `w[]` „w locie” (rolling window 16-elementowe),
     - redukcję duplikacji funkcji,
     - strojenie `__launch_bounds__` / `-maxrregcount` pod konkretną architekturę.

2. **`__umul24` na nowych GPU**
   - Dla wielu architektur pełne 32-bitowe mnożenie bywa równie szybkie lub szybsze, a 24-bit traci sens historyczny.
   - Warto porównać dwie wersje mikrobenchmarkiem.

3. **Dużo gałęzi i atomików na ścieżce zapisu wyniku**
   - `atomicAdd(fcount, 1)` wywoływany do 3 razy na wątek.
   - Przy większej liczbie trafień może to mocno serializować.
   - Usprawnienie: buforowanie per-block (shared memory + prefix sum) i jeden zbiorczy commit do global.

4. **Brak kontroli limitu bufora wynikowego**
   - W `bhashmodUsePrecalc` indeks z `atomicAdd` nie jest porównywany z pojemnością `found/resultPrimorial`.
   - Dodać guard po stronie kernela lub twardy limit po stronie hosta.

## 3) Utrzymanie kodu i czytelność

1. Makro `Zrotr(a,b)` faktycznie robi rotację w lewo, a nie w prawo.
   - Matematycznie może być poprawnie skompensowane stałymi (np. `rol 26 == ror 6`), ale nazwa jest myląca.
   - Zalecenie: zmienić nazewnictwo (`rotl32`) albo użyć `__funnelshift_r`/`__funnelshift_l` i jawnych nazw SHA-256 (`Sigma0`, `Sigma1`, `sigma0`, `sigma1`).

2. `sha256()` i `sha256UsePrecalc()` mają dużą duplikację kodu.
   - Warto wydzielić wspólny rdzeń rund i parametryzować tylko „precalc overrides”.

3. Powtarzające się ręczne kopiowanie tablic (`q[0] = quotient[0] ...`) w `redcify*`.
   - Lepsze: mała helper-funkcja kopiująca 8 limbów lub pętla z `#pragma unroll`.

## 4) Proponowany plan wdrożenia (niskie ryzyko → wysokie korzyści)

1. **Bezpieczna naprawa**: zamienić niejawne 64-bit loady w `sum24()` na jawne składanie z dwóch `uint32_t`.
2. **Poprawność**: wprowadzić pełną walidację modulo dla 53 i 67 (lub dla wszystkich kandydatów po szybkim filtrze 24-bit).
3. **Czytelność**: uporządkować nazwy rotacji/makr SHA i usunąć mylące aliasy.
4. **Wydajność**: profilowanie rejestrów/spilli i test wersji z rolling-window `w[16]`.
5. **Skalowanie zapisu wyników**: ograniczyć contention atomików (block-level compaction).

## 5) Co pomierzyć po zmianach

- czas kernela SHA (`bhashmodUsePrecalc`) i occupancy,
- liczba rejestrów / local memory spill per kernel,
- throughput kandydatów/s,
- zgodność wyników (A/B) względem wersji referencyjnej CPU/GMP,
- liczba fałszywych trafień po poprawce 53/67.
