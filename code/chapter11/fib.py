import time

def fib(x):
    if x < 3:
        return 1
    return fib(x-1) + fib(x-2)

# start = time.perf_counter()
result = fib(40)
# end = time.perf_counter()

# print(result)
# print(end - start)