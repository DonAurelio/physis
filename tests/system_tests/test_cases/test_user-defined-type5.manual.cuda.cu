#include <stdio.h>
#include "cuda.h"
#include "cuda_runtime.h"

#define N 32
#define REAL float

#define OFFSET(x, y, z) ((x) + (y) * N + (z) * N * N)

 __global__ void kernel(REAL *g1, REAL *g2) {
   int x = threadIdx.x + blockIdx.x * blockDim.x;
   int y = threadIdx.y + blockIdx.y * blockDim.y;
   int z = threadIdx.z + blockIdx.z * blockDim.z;

   if (x == 0 || x == N-1 || y == 0 || y == N-1 ||
       z == 0 || z == N-1) return;
  
   float v = g1[OFFSET(x, y, z)] +
       g1[OFFSET(x+1, y, z)] + g1[OFFSET(x-1, y, z)] +
       g1[OFFSET(x, y+1, z)] + g1[OFFSET(x, y-1, z)] +
       g1[OFFSET(x, y, z-1)] + g1[OFFSET(x, y, z+1)];
   g2[OFFSET(x, y, z)] = v;
   return;
}

void dump(REAL *input) {
  int i;
  for (i = 0; i < N*N*N; ++i) {
    printf("%f\n", input[i]);
  }
}

#define halo_width (1)

int main(int argc, char *argv[]) {
  REAL *g1, *g1d;
  REAL *g2, *g2d;
  size_t nelms = N*N*N;
  g1 = (REAL *)malloc(sizeof(REAL) * nelms);
  g2 = (REAL *)malloc(sizeof(REAL) * nelms);  
  cudaMalloc((void**)&g1d, sizeof(REAL) * nelms);
  cudaMalloc((void**)&g2d, sizeof(REAL) * nelms);

  int i;
  for (i = 0; i < (int)nelms; i++) {
    g1[i] = i;
    g2[i] = 0;    
  }
    
  cudaMemcpy(g1d, g1, sizeof(REAL) * nelms, cudaMemcpyHostToDevice);
  cudaMemcpy(g2d, g2, sizeof(REAL) * nelms, cudaMemcpyHostToDevice);  
  
  dim3 block_dim(4, 4, 4);
  dim3 grid_dim(N/block_dim.x, N/block_dim.y, N/block_dim.z);

  kernel<<<grid_dim, block_dim>>>(g1d, g2d);
  cudaMemcpy(g2, g2d, sizeof(REAL) * nelms, cudaMemcpyDeviceToHost);

  dump(g2);

  cudaDeviceReset();
  return 0;
}

