# Analiza telemetry: `hashmod kernel`, `CPU hashmod postprocess`, `sieve`, `fermat`, `copy/sync`

## 1) Szybkie podsumowanie liczb

Dostarczone czasy (ms):

- hashmod kernel = **0.016 ms**
- CPU hashmod postprocess = **0.087 ms**
- sieve = **0.396 ms**
- fermat = **0.243 ms**
- copy/sync = **36.8 ms**

Suma: **37.542 ms**. Udziały procentowe:

- copy/sync: **~98.0%**
- sieve: **~1.05%**
- fermat: **~0.65%**
- CPU hashmod postprocess: **~0.23%**
- hashmod kernel: **~0.04%**

Wniosek: absolutnym bottleneckiem jest **synchronizacja i transfery host↔device**.

---

## 2) Mapowanie każdej metryki na kod i zależności

## `hashmod kernel`

### Gdzie w kodzie
- Dispatch kernela SHA/hashmod (`mHashMod`): `cuLaunchKernel(mHashMod, ...)` w pętli miningu. Plik: `xpm/cuda/xpmclient.cpp`.
- Nazwa kernela wiązana przy inicjalizacji: `"_Z18bhashmodUsePrecalc..."`. Plik: `xpm/cuda/xpmclient.cpp`.
- Implementacja kernela: `__global__ void bhashmodUsePrecalc(...)`. Plik: `xpm/cuda/sha256.cu`.

### Zależności funkcjonalne
- Prekalkulacja SHA po stronie CPU: `precalcSHA256(...)` (używana przed wywołaniem kernela).
- Stałe/limity hashmod (np. `LIMIT13/14/15`, tablice dzielników, primoriale) używane wewnątrz `bhashmodUsePrecalc`.
- Runtime CUDA Driver API (`cuLaunchKernel`, `cuMemcpy*`), opakowane przez `CUDA_SAFE_CALL` i `cudaBuffer`.

### Co sugeruje metryka
0.016 ms jest bardzo niskie → ten etap nie jest bottleneckiem.

---

## `CPU hashmod postprocess`

### Gdzie w kodzie
- Pętla nad wynikami hashmod: `for (unsigned i = 0; i < hashmod.count[0]; ++i)`.
- W tej pętli wykonywane są:
  - podwójne SHA-256 na CPU (`SHA_256 sha; ... sha.final(...)`),
  - operacje GMP (`mpz_set_uint256`, `mpz_divisible_p`, dzielenie primoriali),
  - budowa danych wejściowych do sita (`mpz_export` do `hashBuf`).

Plik: `xpm/cuda/xpmclient.cpp`.

### Zależności funkcjonalne
- `sha256.cpp/.h` (CPU SHA).
- `gmpxx` / GMP (`mpz_class`, `mpz_*`).
- Struktury `hashmod.found`, `hashmod.primorialBitField` zapełniane przez kernel GPU.

### Co sugeruje metryka
0.087 ms jest małe; można poprawić, ale ROI będzie dużo niższe niż przy copy/sync.

---

## `sieve`

### Gdzie w kodzie
- Dispatch trzech etapów sita:
  - `mSieveSetup` (kernel `setup_sieve`),
  - `mSieve` (kernel `sieve`),
  - `mSieveSearch` (kernel `s_sieve`).

Wszystkie odpalane w pętli `for (i < mSievePerRound)` w `xpm/cuda/xpmclient.cpp`.

- Implementacje:
  - `setup_sieve(...)` w `xpm/cuda/fermat.cu`,
  - `sieve(...)` i `s_sieve(...)` w `xpm/cuda/sieve.cu`.

### Zależności funkcjonalne
- Parametry z `config_t` (SIZE/STRIPES/WIDTH/PCOUNT/TARGET).
- Bufory `primeBuf`, `primeBuf2`, `modulosBuf`, `sieveBuf`, `sieveOff`, `candidatesCountBuffers`.
- Konfiguracja runtime (np. `sieveSize`, `weaveDepth`, `width`, `sievePerRound`) z `xpm/cuda/config.txt`.

### Co sugeruje metryka
0.396 ms: drugi największy etap obliczeniowy, ale i tak skrajnie mniejszy od copy/sync.

---

## `fermat`

### Gdzie w kodzie
- Funkcja `FermatDispatch(...)` w `xpm/cuda/xpmclient.cpp`:
  - przygotowanie danych (`mFermatSetup`),
  - właściwy test (`mFermatKernel320`/`mFermatKernel352`),
  - filtracja/kompaktowanie (`mFermatCheck`).

- Implementacje kerneli:
  - `setup_fermat`, `fermat_kernel`, `fermat_kernel320`, `check_fermat` w `xpm/cuda/fermat.cu`.

### Zależności funkcjonalne
- Dane kandydatów z sita (`sieveBuffers`, `candidatesCountBuffers`).
- Bufor hashy (`hashBuf`) i parametry głębokości (`mDepth`).

### Co sugeruje metryka
0.243 ms: umiarkowanie małe; optymalizacja ma sens dopiero po usunięciu copy/sync bottleneck.

---

## `copy/sync`

### Gdzie w kodzie
Najbardziej krytyczny fragment w pętli miningu:

- Jawna synchronizacja: `cuEventSynchronize(sieveEvent)`.
- Seria transferów DtoH po każdej iteracji:
  - `candidatesCountBuffers[i][widx].copyToHost(mSieveStream)` w pętli,
  - `hashmod.found.copyToHost(...)`, `hashmod.primorialBitField.copyToHost(...)`, `hashmod.count.copyToHost(...)`,
  - `fermat320/352.buffer[widx].count.copyToHost(...)`,
  - `final.info.copyToHost(...)`, `final.count.copyToHost(...)`.

W kodzie jest komentarz, że `copyToHost (cuMemcpyDtoHAsync)` de facto blokuje. To praktycznie potwierdza telemetry.

Dodatkowo API buforów (`cudaBuffer`) używa zwykłego `new[]` dla hosta, a nie pamięci page-locked (pinned), co pogarsza throughput i semantykę async przy HtoD/DtoH.

### Zależności funkcjonalne
- `cudaBuffer` i metody `copyToHost/copyToDevice` (`cudautil.h`).
- CUDA streams/events (`mSieveStream`, `mHMFermatStream`, `sieveEvent`).

### Co sugeruje metryka
36.8 ms = ~98% czasu całej iteracji. To jest główny i zdecydowanie największy bottleneck.

---

## 3) Rekomendacje usprawnień (priorytet wg ROI)

## Priorytet P0 (największy zysk): copy/sync

1. **Zmniejszyć liczbę synchronizacji na iterację**
   - Zamiast pełnego `cuEventSynchronize(sieveEvent)` co iterację, użyć pipeline’u N+1 i odczytywać wyniki z poprzedniej iteracji (double/triple buffering).
   - Ograniczyć globalne „barriers” do momentów, gdy dane są naprawdę potrzebne przez CPU.

2. **Przejść na pinned host memory dla buforów często kopiowanych**
   - Zmienić `cudaBuffer` tak, by dla buforów transferowych alokować host memory przez `cuMemAllocHost` / `cudaHostAlloc`.
   - Szczególnie: `hashmod.*`, `candidatesCountBuffers`, `fermat*.count`, `final.*`.

3. **Skonsolidować transfery**
   - Zamiast wielu małych `copyToHost` wykonać mniej, większych kopii (np. spakować liczniki i wyniki do jednego/paru buforów).
   - Małe transfery + sync zabijają latency.

4. **Ograniczyć ilość danych kopiowanych do CPU**
   - Nie kopiować pełnych `found`/`final.info` przy braku kandydatów (najpierw kopiować tylko count, warunkowo payload).
   - Rozważyć GPU-side prefilter i dopiero potem transfer skróconej listy.

Szacowany efekt: nawet częściowe wdrożenie P0 może dać **wielokrotny** wzrost przepustowości, bo dotyka ~98% czasu.

## Priorytet P1: CPU postprocess hashmod

1. **Równoleglenie CPU postprocess**
   - Pętla po `hashmod.count[0]` nadaje się do wielowątkowości (np. worker pool), szczególnie część SHA+GMP.

2. **Przenieść walidację na GPU (częściowo)**
   - Część logiki dzielników/primoriali już jest na GPU; można jeszcze zredukować CPU mpz-work przez bardziej agresywny filtr po stronie kernela.

3. **Unikać nadmiarowych konwersji GMP**
   - Caching obiektów i pamięci tymczasowej, ograniczenie `mpz_import/export` tam, gdzie możliwe.

## Priorytet P2: sieve + fermat

1. **Auto-tuning runtime**
   - Dobrać `sievePerRound`, `weaveDepth`, `windowSize`, `width` pod konkretny GPU przez krótki warmup i wybór profilu.

2. **Sieve kernel**
   - Sprawdzić contention na `atomicOr` (szczególnie w gałęziach z małym krokiem prime).
   - Rozważyć lokalną kompresję bitmask i rzadsze atomiki/global writes.

3. **Fermat kernel occupancy**
   - Zweryfikować occupancy i register pressure dla 320/352, ewentualnie podział kernela lub modyfikacja launch geometry.

---

## 4) Proponowana kolejność wdrożeń

1. Pinned memory + redukcja sync/transferów (P0).
2. Warunkowe kopiowanie payloadów (count -> payload on-demand).
3. Pipeline N+1 (overlap GPU compute z transferem poprzedniej iteracji).
4. Dopiero potem strojenie sieve/fermat i CPU postprocess.

To da największy zwrot przy najmniejszym ryzyku regresji jakości wyników.
