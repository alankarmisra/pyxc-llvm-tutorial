MODV = 1_000_000_007
N = 80_000
STEPS = 800


class Vec2:
    __slots__ = ("x", "y")

    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y


class Particle:
    __slots__ = ("id", "pos", "vel", "bins")

    def __init__(self, pid: int) -> None:
        self.id = pid
        self.pos = Vec2(pid, pid * 2)
        self.vel = Vec2(1 + pid, 2 - pid)
        self.bins = [pid + 0, pid + 1, pid + 2, pid + 3]


def main() -> None:
    ps = [Particle(i) for i in range(N)]

    step = 0
    while step < STEPS:
        i = 0
        while i < N:
            p = ps[i]
            p.pos.x = p.pos.x + p.vel.x
            p.pos.y = p.pos.y + p.vel.y
            slot = step % 4
            p.bins[slot] = p.bins[slot] + i + step
            i += 1
        step += 1

    checksum = 0
    i = 0
    while i < N:
        p = ps[i]
        checksum = (checksum + p.pos.x + p.pos.y + p.bins[0] + p.bins[1] + p.bins[2] + p.bins[3]) % MODV
        i += 1

    print(checksum)


if __name__ == "__main__":
    main()
