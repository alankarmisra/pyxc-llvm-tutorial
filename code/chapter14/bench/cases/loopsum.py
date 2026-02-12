def loopsum(n: int, m: int) -> int:
    total = 0
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            total += i * j
    return total


def main() -> None:
    print(f"{float(loopsum(10000, 10000)):.6f}")


if __name__ == "__main__":
    main()
