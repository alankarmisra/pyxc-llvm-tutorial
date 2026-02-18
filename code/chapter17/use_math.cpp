#include <cstdio>
#include <cmath>

// Declare functions compiled from Pyxc
extern "C" {
  double square(double x);
  double add(double x, double y);
  double mul(double x, double y);
  double distance(double x1, double y1, double x2, double y2);
}

int main() {
  printf("Calling Pyxc math functions from C++:\n\n");

  double x = 5.0;
  printf("square(%.1f) = %.1f\n", x, square(x));

  printf("add(3.0, 4.0) = %.1f\n", add(3.0, 4.0));
  printf("mul(6.0, 7.0) = %.1f\n", mul(6.0, 7.0));

  double x1 = 0.0, y1 = 0.0;
  double x2 = 3.0, y2 = 4.0;
  double dist = distance(x1, y1, x2, y2);
  printf("distance((%.1f,%.1f), (%.1f,%.1f)) = %.1f\n", x1, y1, x2, y2, dist);
  printf("  (sqrt of that is %.2f)\n", sqrt(dist));

  return 0;
}
