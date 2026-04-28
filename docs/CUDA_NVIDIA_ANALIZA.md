# Analiza programu pod kątem NVIDIA/CUDA i możliwych usprawnień wydajności

## Zakres analizy
Ta analiza opisuje aktualny sposób wykorzystania NVIDIA CUDA w projekcie `xpmclientnv` oraz proponuje usprawnienia wydajnościowe o największym potencjale.

## Jak program korzysta z NVIDIA i CUDA

### 1) Budowanie i linkowanie ścieżki CUDA
- CUDA jest wykrywana przez `find_package(CUDAToolkit)` z fallbackiem do legacy `find_package(CUDA)`.
- Tworzony jest target `xpmclientnv`, który linkuje `CUDA::cuda_driver` i `CUDA::nvrtc` (lub ręcznie `libcuda`/`libnvrtc`).
- Pliki `xpm/cuda/*.cu` są kopiowane po buildzie do katalogu binarnego, bo kompilacja kerneli odbywa się runtime przez NVRTC.

**Wniosek:** architektura jest oparta o kompilację JIT (NVRTC), a nie klasyczny offline `nvcc` + fatbinary.

### 2) Kompilacja runtime kerneli (NVRTC)
- Na starcie generowany jest `xpm/cuda/config.cu` z parametrami kernela (`STRIPES`, `WIDTH`, `PCOUNT`, `TARGET`, `SIZE`, `LSIZE`, limity).
- Do NVRTC podawana jest opcja `--gpu-architecture=compute_XY` na podstawie compute capability każdej karty.
- Kod kernela jest składany z wielu plików `.cu` i zapisany do `kernelxpm_gpu<N>.ptx`, a potem ładowany przez `cuModuleLoadDataEx`.

**Wniosek:** to podejście daje elastyczność, ale ma koszt inicjalizacji i ryzyko niedostrojonego kodu SASS względem konkretnego SM.

### 3) Pipeline obliczeń na GPU
Wątek górnika realizuje iteracyjny pipeline:
1. `bhashmodUsePrecalc` (hashmod) na strumieniu `mHMFermatStream`.
2. `setup_sieve` + 2x `sieve` + `s_sieve` (szukanie kandydatów) na strumieniu `mSieveStream`.
3. `setup_fermat` + `fermat_kernel320/352` + `check_fermat` na `mHMFermatStream`.
4. Asynchroniczne kopie liczników i części buforów host/device.

Są dwa strumienie CUDA i mechanizm N+1 (pendingCopy), ale w każdej iteracji pojawiają się pełne synchronizacje strumieni (`cuStreamSynchronize`).

### 4) Charakterystyka kerneli
- `sieve.cu` intensywnie używa `atomicOr` do oznaczania bitów w sicie (współdzielona pamięć + dużo kolizji).
- `s_sieve` używa `atomicAdd` do dopisywania kandydatów (320/352).
- `fermat.cu` zawiera bardzo ciężką arytmetykę wielosłowową (Montgomery, dużo rejestrów, duża presja na occupancy).
- `sha256.cu` używa tablic `__constant__`, pełnego rozwinięcia rund i częściowo starszych idiomów (np. `__umul24`).

## Główne wąskie gardła (priority)

## P1 — Synchronizacje CPU↔GPU i kopiowanie host/device
Największa strata prawdopodobnie wynika z przerywania asynchroniczności:
- Synchronizacja obu strumieni przed odczytami,
- Częste kopiowanie małych liczników + warunkowe kopiowanie większych buforów,
- Część walidacji kandydatów (SHA + GMP) realizowana po stronie CPU.

**Potencjał zysku:** wysoki (często 10–30% w pipeline’ach mieszanych, zależnie od GPU i obciążenia CPU).

### Rekomendacje
1. Zastąpić część `cuStreamSynchronize` logiką event-driven (`cuEventQuery`/`cuStreamWaitEvent`).
2. Utrzymać „always-in-flight” 2–3 iteracje zamiast twardego czekania co iterację.
3. Spiąć kopiowanie liczników w jeden staging buffer i jedną kopię asynchroniczną.
4. Rozważyć przeniesienie części postprocessingu (np. filtracji kandydatów) na GPU przed transferem do hosta.

## P2 — Atomiki w `sieve.cu`
`atomicOr` jest wykonywany bardzo często i do potencjalnie tych samych słów, co powoduje serializację.

**Potencjał zysku:** wysoki/średni (często 10–25% dla fazy sita).

### Rekomendacje
1. Przebudować zapis sita na model warp-aggregated:
   - lokalna akumulacja maski bitowej per warp,
   - mniej atomików (1 atomik na segment zamiast wielu na pojedyncze bity).
2. Rozważyć podział sita na „tile” per blok z finalnym merge (ograniczenie contention).
3. Przetestować wariant bez atomików dla części zakresu (np. prywatne segmenty + redukcja).

## P3 — Occupancy i presja rejestrów w Fermat/Montgomery
Kerneli `fermat_kernel320/352` są obliczeniowo ciężkie, a duża liczba rejestrów może obniżać liczbę aktywnych warpów.

**Potencjał zysku:** średni/wysoki.

### Rekomendacje
1. Profilować `registers per thread`, `achieved occupancy`, `stall_long_scoreboard` (Nsight Compute).
2. Dodać tuning NVRTC per arch:
   - `--use_fast_math` (tylko jeśli bez wpływu na poprawność),
   - `--maxrregcount=<N>` (A/B test),
   - `--fmad=true`.
3. Rozważyć dwa profile kernela:
   - profil "throughput" (więcej occupancy),
   - profil "latency" (więcej ILP).

## P4 — Parametry launch i auto-tuning
Aktualnie parametry (`LSIZE`, `TARGET`, `PCOUNT`, `SIZE`) są statyczne i globalnie ustawiane, z częściową auto-korektą.

**Potencjał zysku:** średni.

### Rekomendacje
1. Dodać krótki autotuning przy starcie (30–60 s) dla każdej GPU:
   - `LSIZE` (256/512/1024),
   - `mSievePerRound`,
   - współczynnik `numHashCoeff`,
   - `TARGET/WIDTH` w dopuszczalnym zakresie.
2. Zapisać profil wydajności per model GPU i sterownik.

## P5 — JIT NVRTC i format wyjściowy
NVRTC generuje PTX, a driver JIT kompiluje dalej. Brakuje cache binarek natywnych zależnych od wersji sterownika/GPU.

**Potencjał zysku:** niski dla steady-state, średni dla startup/restartów.

### Rekomendacje
1. Wdrożyć cache modułów zależny od:
   - GPU UUID / SM version,
   - wersji drivera,
   - hasha źródeł + config.
2. Rozważyć alternatywę: offline build cubin/fatbin dla popularnych SM + fallback NVRTC.

## Kompatybilność i ryzyka na nowoczesnych kartach NVIDIA
1. Kod używa starszych idiomów (`__umul24`) — na nowych architekturach zwykle bez korzyści, czasem z regresją.
2. Mieszanie pracy CPU/GPU (GMP + SHA na CPU) ogranicza skalowanie przy większej liczbie GPU.
3. Potencjalna duplikacja ścieżek kodu (sekcje mining + benchmark) zwiększa ryzyko niespójnych optymalizacji.

## Proponowany plan optymalizacji (kolejność wdrożenia)
1. **Profiling baseline** (Nsight Systems + Nsight Compute): zebrać timeline i metryki occupancy/atomics.
2. **Usunięcie twardych synchronizacji** i przejście na event chaining.
3. **Optymalizacja sita** (redukcja atomików, warp aggregation).
4. **Tuning Fermat** (rejestry/occupancy, opcje NVRTC per GPU).
5. **Autotuner startowy** i zapis profilu.
6. **CPU offload reduction** (więcej walidacji na GPU).

## Co mierzyć po zmianach (KPI)
- Hashmod kernel time [ms]
- Sieve kernel time [ms]
- Fermat kernel time [ms]
- Copy/sync time [ms]
- FPS (testCount/elapsed)
- CPD
- Udział czasu CPU w postprocessingu

## Oczekiwany efekt biznesowy
Przy typowym układzie bottlenecków (atomiki + synchronizacje + CPU postprocess) realistyczny łączny zysk po etapach P1–P4 to **~20–50%** throughputu, zależnie od modelu GPU i jakości aktualnego dostrojenia parametrów.
