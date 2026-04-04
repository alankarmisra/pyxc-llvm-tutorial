import time

def fib(n: int) -> int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

n = 41
start = time.perf_counter()
result = fib(n)
elapsed = time.perf_counter() - start
print(result)
print(f"elapsed={elapsed:.6f}s")
