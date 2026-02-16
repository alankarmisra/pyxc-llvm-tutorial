def fib(n: int) -> int:
    if n < 3:
        return 1
    return fib(n - 1) + fib(n - 2)


def main() -> None:
    print(f"{float(fib(41)):.6f}")


if __name__ == "__main__":
    main()
