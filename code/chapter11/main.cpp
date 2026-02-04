// clang++ main.cpp fib.o -o main
#include <iostream>

extern "C" {
double fib(double);
}

int main() { std::cout << fib(10.0) << std::endl; }
