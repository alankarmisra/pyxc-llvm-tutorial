import time

WIDTH = 1000
HEIGHT = 1000
MAX_ITER = 500


def mandel_checksum(width: int, height: int, max_iter: int) -> int:
    total = 0
    inv_w = 1.0 / width
    inv_h = 1.0 / height
    for y in range(height):
        ci = (y * inv_h) * 2.0 - 1.0
        for x in range(width):
            cr = (x * inv_w) * 3.5 - 2.5
            zr = 0.0
            zi = 0.0
            escaped = False
            it = 0
            for i in range(max_iter):
                if escaped:
                    pass
                else:
                    zr2 = zr * zr - zi * zi + cr
                    zi2 = 2.0 * zr * zi + ci
                    zr, zi = zr2, zi2
                    if (zr * zr + zi * zi) > 4.0:
                        escaped = True
                        it = i
            if not escaped:
                it = max_iter
            total += it
    return total

start = time.perf_counter()
result = mandel_checksum(WIDTH, HEIGHT, MAX_ITER)
elapsed = time.perf_counter() - start
print(result)
print(f"elapsed={elapsed:.6f}s")
