#include <cstdio>

static long long mandel_checksum(int width, int height, int max_iter) {
  long long total = 0;
  double inv_w = 1.0 / (double)width;
  double inv_h = 1.0 / (double)height;
  for (int y = 0; y < height; ++y) {
    double ci = ((double)y * inv_h) * 2.0 - 1.0;
    for (int x = 0; x < width; ++x) {
      double cr = ((double)x * inv_w) * 3.5 - 2.5;
      double zr = 0.0;
      double zi = 0.0;
      int escaped = 0;
      int it = 0;
      for (int i = 0; i < max_iter; ++i) {
        if (!escaped) {
          double zr2 = zr * zr - zi * zi + cr;
          double zi2 = 2.0 * zr * zi + ci;
          zr = zr2;
          zi = zi2;
          if (zr * zr + zi * zi > 4.0) {
            escaped = 1;
            it = i;
          }
        }
      }
      if (!escaped) it = max_iter;
      total += it;
    }
  }
  return total;
}

int main() {
  int width = 1000;
  int height = 1000;
  int max_iter = 500;
  long long result = mandel_checksum(width, height, max_iter);
  std::printf("%lld\n", result);
  return 0;
}
