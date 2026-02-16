# Analiza wydajności CUDA (xpm/cuda) i rekomendacje

## Największe bottlenecki

1. **Wysoka kontencja na `atomicOr` w `sieve.cu`** – ten kernel wykonuje bardzo dużo atomików do współdzielonej tablicy `sieve[]`, często w pętlach, co powoduje serializację i spadek throughputu.
2. **Bardzo duże użycie rejestrów i presja occupancy** – szczególnie w `s_sieve` (lokalne tablice `tmp1/tmp2`) i ciężkich ścieżkach arytmetyki wielkiej precyzji (`procs.cu`, `fermat.cu`).
3. **Stare intrinsics i wzorce pod starsze GPU (`__umul24`)** – na nowszych architekturach zwykłe mnożenie 32-bit bywa równie szybkie lub szybsze.
4. **Brak wymuszonej optymalizacji SASS pod architekturę (`sm_XX`)** – obecnie kompilacja używa `compute_XX` (PTX), więc finalna jakość kodu zależy od JIT sterownika.
5. **Twardo ustawione konfiguracje launchu i heurystyki (`mBlockSize = SM * 4 * 64`)** bez occupancy-based autotuningu.

## Konkretne miejsca w kodzie

- `xpm/cuda/sieve.cu`
  - Inicjalizacja i masowe `atomicOr` do shared memory (`sieve[]`) oraz pętle z atomikami to główny hotspot.
  - Dodatkowo wielokrotnie liczony jest ten sam wzorzec normalizacji `pos`.
- `xpm/cuda/sieve.cu` (`s_sieve`)
  - Lokalna alokacja `uint32_t tmp1[WIDTH]` i `tmp2[WIDTH]` zwiększa zużycie rejestrów/local memory.
  - Trzykrotna logika skanowania masek jest podobna i może być częściowo zfuzowana.
- `xpm/cuda/xpmclient.cpp`
  - NVRTC dostaje tylko `--gpu-architecture=compute_XY`; brak jawnych flag wydajnościowych i brak ścieżki na cubin (`sm_XY`).
  - Stały dobór rozmiaru partii testów Fermata i launch dimensions.
- `cudautil.cpp`
  - Ładowanie PTX z JIT może działać gorzej niż prekompilowany kod SASS dla konkretnej architektury.

## Rekomendacje (od największego efektu)

### 1) Ogranicz atomiki w `sieve` (największy potencjał)

- **Batchowanie bitów per-thread/per-warp**: zamiast `atomicOr` dla każdego trafienia, akumuluj OR maski dla tego samego słowa i wykonuj jeden `atomicOr` na słowo.
- **Warp-level agregacja**: użyj `__ballot_sync` / `__shfl_sync` do złożenia maski warp i pojedynczego zapisu.
- **Dwufazowe sitowanie**:
  1. Każdy warp zapisuje lokalnie do bufora (bez atomików, gdy możliwe).
  2. Redukcja buforów do `sieve[]` z dużo mniejszą liczbą atomików.

Oczekiwany efekt: duży wzrost przepustowości, bo atomiki są tu dominującym kosztem.

### 2) Zmniejsz presję rejestrów i podnieś occupancy

- Zprofiluj i porównaj `--maxrregcount=64`, `56`, `48` dla kluczowych kerneli (`sieve`, `s_sieve`, `fermat_kernel*`).
- Rozbij `s_sieve` na 2 mniejsze kernele (np. oddzielnie cunningham1/2 i bitwin), jeśli jeden monolityczny kernel zabija occupancy.
- Dla `tmp1/tmp2[WIDTH]` rozważ:
  - mniejsze tile, przetwarzanie oknami,
  - przechowywanie części danych w shared memory,
  - redukcję szerokości `WIDTH` przez autotuning.

### 3) Zamień `__umul24` na normalne 32-bit multiply (test A/B)

- W wielu miejscach (`sieve.cu`, `sha256.cu`) użycie `__umul24` jest historyczne.
- Na nowszych GPU (Pascal+ / Turing+ / Ampere+) często lepiej działa zwykłe `a*b` (lub `__umulhi` tam gdzie potrzebne).
- Zrób benchmark A/B na Twoich kartach – to szybka zmiana z potencjalnym zyskiem.

### 4) Popraw pipeline kompilacji NVRTC

- Dodaj opcje kompilacji:
  - `--use_fast_math` (jeśli akceptowalna semantyka),
  - `--extra-device-vectorization`,
  - `--fmad=true`.
- Rozważ tryb produkcyjny: budowanie CUBIN dla `sm_XY` i ładowanie `cuModuleLoadFatBinary`/`cuModuleLoad` (bez zależności od JIT PTX).
- Trzymaj oddzielne cache dla każdej architektury i wariantu flag.

### 5) Autotuning parametrów uruchomienia

- Teraz część parametrów jest statyczna (`LSIZE`, `WIDTH`, `TARGET`, `SIEVERANGE*`, `mBlockSize`).
- Dodaj krótki autotuner przy starcie:
  - skanuje np. `LSIZE` 128/256/512,
  - kilka wartości `WIDTH` i `sievePerRound`,
  - wybiera konfigurację po czasie 1–2 s microbench.

### 6) Pamięć i transfery

- Sprawdź, czy wszystkie host buffer copy są asynchroniczne i pinned tam, gdzie to daje zysk.
- Rozważ podwójne buforowanie (`double-buffer`) większej liczby etapów (już częściowo jest, można pogłębić overlap).
- Zmniejsz ilość resetów/zerowań dużych buforów, gdy można nadpisywać selektywnie.

## Plan profilowania (praktyczny)

1. Uruchom `nsys` i `ncu` dla 60–120 s stabilnego workloadu.
2. Dla `sieve` i `s_sieve` zbierz:
   - `sm__throughput`,
   - `l1tex__data_bank_conflicts`,
   - `shared_store_transactions`,
   - `atomic_transactions`,
   - occupancy i register count.
3. Wykonaj A/B:
   - baseline,
   - redukcja atomików,
   - bez `__umul24`,
   - z `--maxrregcount`.
4. Zatrzymaj tylko zmiany z poprawą hashrate/J-per-share, nie tylko czasu pojedynczego kernela.

## Szybkie “quick wins” na start

1. Benchmark A/B `__umul24` vs zwykłe `*`.
2. Dodanie `--use_fast_math --fmad=true` do NVRTC (A/B).
3. Ograniczenie `atomicOr` przez prostą lokalną agregację (nawet bez pełnej refaktoryzacji).
4. Test `--maxrregcount` dla `s_sieve`.

---

Jeśli chcesz, w kolejnym kroku mogę przygotować konkretny patch z **jedną bezpieczną zmianą A/B** (np. wariant bez `__umul24` + flaga kompilacji), tak żebyś mógł od razu zmierzyć zysk na swojej karcie.
