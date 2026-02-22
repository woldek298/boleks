# Audyt wydajności (XPM CUDA/OpenCL)

## Punkt wyjścia (Twoja telemetria)
`hashmod kernel=1.492 ms, CPU hashmod postprocess=0.030 ms, sieve=36.154 ms, fermat=10.789 ms, copy/sync=35.676 ms`

Wniosek: największy zysk jest w **sieve** i **copy/sync**. Hashmod i CPU postprocess są relatywnie małe.

## Największe bottlenecki znalezione w kodzie

1. **Silna serializacja przez synchronizacje strumieni** (`cuStreamSynchronize`) w pętli miningu.
   - Aktualnie czekasz na `mSieveStream`, potem na `mHMFermatStream`, potem ponownie czekasz po kopiowaniu wyników host/device.
   - To tłumaczy wysoki czas `copy/sync`.

2. **Sieve używa bardzo wielu `atomicOr` do pamięci współdzielonej**.
   - W `xpm/cuda/sieve.cu` większość czasu schodzi na atomikach i konfliktach banków shared memory.
   - Przy dużym `SIZE/STRIPES/WIDTH` jest to klasyczny hotspot.

3. **Nadmiar transferów H↔D i "round-trip" sterowania z CPU**.
   - Liczniki i bufory kandydatów są stale kopiowane host-side i natychmiast wykorzystywane do kolejnego dispatchu.
   - To ogranicza overlap kerneli i transferów.

4. **Konfiguracja `mSievePerRound` reaguje wolno i może pompować koszt sieve**.
   - Gdy kandydatów brakuje, rośnie liczba rund sita, ale bez twardego ograniczenia opartego o rzeczywisty occupancy/throughput GPU.

5. **Sito i wyszukiwanie kandydatów są uruchamiane sekwencyjnie per hash**.
   - Obecnie każda iteracja dispatchuje setup + 2x sieve + search dla kolejnych hashy.
   - Da się to przepiąć na bardziej batchowy/pipeline’owy model.

## Priorytety optymalizacji (kolejność wdrożenia)

### P1: Zmniejszenie `copy/sync` (najtańsze ROI)
- Zamień globalne `cuStreamSynchronize` na **event-driven fencing** (`cuEventRecord` + `cuEventQuery`/`cuStreamWaitEvent`) i przetwarzanie tylko gotowych etapów.
- Trzymaj liczniki pipeline’u po stronie GPU dłużej (host ma pobierać jedynie finalne minima danych).
- Wydziel osobny strumień na transfery D→H (copy stream), żeby unikać zatrzymywania streamu obliczeniowego.

Spodziewany efekt: zauważalny spadek `copy/sync` (często 20–50% tego segmentu).

### P2: Ograniczenie atomików w `sieve.cu`
- Dla małych prime/dużych konfliktów: lokalna akumulacja bitmask na warp/block i rzadszy zapis do shared.
- Rozważ wariant: lane-local bitset + redukcja warpowa zamiast 4x `atomicOr` w pętli.
- Przetestuj alternatywne `LSIZE` (512 vs 1024), bo `atomic` contention potrafi spaść bardziej niż strata occupancy.

Spodziewany efekt: największy potencjał redukcji `sieve`.

### P3: Lepszy overlap hashmod/sieve/fermat
- Utrzymuj co najmniej 2–3 "flight" batchy w pipeline, zamiast twardego kroku iteracyjnego zależnego od host sync.
- Pozwól sieve i fermat pracować równolegle na różnych batchach (przez eventy między streamami).

Spodziewany efekt: lepsze wykorzystanie GPU i mniejszy czas ściany iteracji.

### P4: Autotuning runtime
- Dodaj prosty autotuner dla `mSievePerRound`, `numHashCoeff`, `mLSize` oparty o krótkie okna telemetryczne (np. 30–60 s).
- Celem powinno być max `shares/day` lub `candis/sec`, nie pojedynczy czas kernela.

## Konkretne miejsca w projekcie do zmiany
- `xpm/cuda/xpmclient.cpp`:
  - logika `pendingCopy` i synchronizacji streamów,
  - harmonogram dispatchu hashmod/sieve/fermat,
  - adaptacja `mSievePerRound`.
- `xpm/cuda/sieve.cu`:
  - sekcje z `atomicOr` w pętlach (największy hotspot kernela).
- `xpm/cuda/fermat.cu`:
  - wtórny hotspot; po poprawie sieve/copy-sync może stać się kolejnym celem.

## Proponowany plan benchmarków po zmianach
1. Dodać telemetrykę per-faza + utilization GPU (Nsight Systems/Compute).
2. Uruchamiać A/B po 5–10 min na identycznym worku i ustawieniach.
3. Kryteria sukcesu:
   - `copy/sync` ↓,
   - `sieve` ↓,
   - `fps`/`cpd` ↑,
   - brak regresji share acceptance.

## Szybkie "quick wins" bez dużej przebudowy
- Ograniczyć pełne synchronizacje do miejsc absolutnie koniecznych.
- Zmniejszyć częstotliwość kopiowania większych buforów D→H (kopiować tylko przy niezerowych licznikach i progach).
- Przetestować 2 profile: `LSIZE=512` i `LSIZE=1024` z Twoją kartą.
- Dobrać `sievePerRound` pod konkretny GPU model (zamiast stałej wartości startowej).
