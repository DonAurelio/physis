/*
 * TEST: Copy between user-defined type members
 * DIM: 3
 * PRIORITY: 1
 */

#include <stdio.h>
#include "physis/physis.h"

#define N 32
#define ITER 10

struct Point {
  float p;
  float q;
};

DeclareGrid3D(Point, struct Point);

void kernel(const int x, const int y, const int z,
            PSGrid3DPoint g) {
  float v = PSGridGet(g, x, y, z).p;
  PSGridEmitUtype(g.q, v);
  return;
}

void check(struct Point *p) {
  int i;
  for (i = 0; i < N*N*N; ++i) {
    if (p[i].p != p[i].q) {
      fprintf(stderr, "Error: mismatch at %d, in: %f, out: %f\n",
              i, p[i].p, p[i].q);
      exit(1);
    }
  }
}

int main(int argc, char *argv[]) {
  PSInit(&argc, &argv, 3, N, N, N);
  PSGrid3DPoint g = PSGrid3DPointNew(N, N, N);

  PSDomain3D d = PSDomain3DNew(0, N, 0, N, 0, N);
  size_t nelms = N*N*N;
  
  struct Point *indata = (struct Point *)malloc(
      sizeof(struct Point) * nelms);
  int i;
  for (i = 0; i < nelms; i++) {
    indata[i].p = i;
    indata[i].q = 0;
  }
    
  PSGridCopyin(g, indata);

  PSStencilRun(PSStencilMap(kernel, d, g));
    
  PSGridCopyout(g, indata);

  check(indata);

  PSGridFree(g);
  PSFinalize();
  free(indata);
  return 0;
}

