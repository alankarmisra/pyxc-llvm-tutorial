def divisible(n: int, d: int) -> bool:
    if n < d:
        return False
    if n < d + 1:
        return True
    return divisible(n - d, d)


def is_prime_from(n: int, d: int) -> bool:
    if d * d > n:
        return True
    if divisible(n, d):
        return False
    return is_prime_from(n, d + 1)


def is_prime(n: int) -> bool:
    if n < 2:
        return False
    return is_prime_from(n, 2)


def count_primes(limit: int) -> int:
    count = 0
    for n in range(2, limit + 1):
        if is_prime(n):
            count += 1
    return count


def main() -> None:
    total = 0
    for _ in range(10):
        total += count_primes(1900)
    print(f"{float(total):.6f}")


if __name__ == "__main__":
    main()
