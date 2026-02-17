N = 200_000_000
MODV = 2_147_483_647


def main() -> None:
    x = 1_234_567
    acc = 0
    i = 0

    while i < N:
        x = (x * 48_271) % MODV
        acc = (acc + x + i) % MODV
        i += 1

    print(acc)


if __name__ == "__main__":
    main()
