#!/usr/bin/env python3
"""Simple interactive CPD calculator for Primecoin tuning."""

TARGET = 10
SECONDS_PER_DAY = 24 * 3600


def calculate_cpd(fermat_per_sec: float, primes: float, target: int = TARGET) -> float:
    return SECONDS_PER_DAY * fermat_per_sec * (primes ** target)


def read_float(prompt: str) -> float:
    while True:
        raw = input(prompt).strip().replace(',', '.')
        try:
            value = float(raw)
            if value < 0:
                print("Wpisz liczbę >= 0.")
                continue
            return value
        except ValueError:
            print("Niepoprawna liczba, spróbuj ponownie.")


def main() -> None:
    print("Kalkulator CPD")
    print(f"Wzór: cpd = 86400 * fermat * primes^{TARGET}")
    print("Wpisz 'q' aby zakończyć.\n")

    while True:
        first = input("Podaj fermat (/sec): ").strip()
        if first.lower() in {"q", "quit", "exit"}:
            print("Koniec.")
            return

        try:
            fermat = float(first.replace(',', '.'))
            if fermat < 0:
                print("Fermat musi być >= 0.\n")
                continue
        except ValueError:
            print("Niepoprawna liczba fermat.\n")
            continue

        primes = read_float("Podaj primes (np. 0.105): ")
        cpd = calculate_cpd(fermat, primes)
        print(f"CPD = {cpd:.2f} / day\n")


if __name__ == "__main__":
    main()
