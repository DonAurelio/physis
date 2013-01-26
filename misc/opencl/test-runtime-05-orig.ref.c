#define PHYSIS_REF 
#include "physis/physis.h"
#include <math.h>
#define SIZEX 64
#define SIZEY 64
#define SIZEZ 64
#define NDIM  3

inline void kernel1(const long x,const long y,const long z,PSGrid3DFloat g)
{
  float v = (((float *)(g -> p0))[x + y * PSGridDim(g,0) + z * PSGridDim(g,0) * PSGridDim(g,1)] * 2);
  ((float *)(g -> p0))[x + y * PSGridDim(g,0) + z * PSGridDim(g,0) * PSGridDim(g,1)] = v;
}

struct __PSStencil_kernel1 
{
  __PSDomain dom;
  PSGrid3DFloat g;
  int __g_index;
}
;

static struct __PSStencil_kernel1 __PSStencilMap_kernel1(__PSDomain dom,PSGrid3DFloat g)
/* Generated by generateMap */
{
  struct __PSStencil_kernel1 stencil = {dom, g, __PSGridGetID(g)};
  return stencil;
}

static void __PSStencilRun_kernel1(struct __PSStencil_kernel1 *s)
/* Generated by generateRunKernelBody */
{
  unsigned int i2;
  for (i2 = s -> dom.local_min[2]; i2 < s -> dom.local_max[2]; ++i2) {
    unsigned int i1;
    for (i1 = s -> dom.local_min[1]; i1 < s -> dom.local_max[1]; ++i1) {
      unsigned int i0;
      for (i0 = s -> dom.local_min[0]; i0 < s -> dom.local_max[0]; ++i0) {
        kernel1(i0,i1,i2,s -> g);
      }
    }
  }
}

inline float plus10(float v)
{
  return (v + 10.0);
}

inline void kernel2(const long x,const long y,const long z,PSGrid3DFloat g)
{
  float v = ((float *)(g -> p0))[x + y * PSGridDim(g,0) + z * PSGridDim(g,0) * PSGridDim(g,1)];
  v = plus10(v);
  ((float *)(g -> p0))[x + y * PSGridDim(g,0) + z * PSGridDim(g,0) * PSGridDim(g,1)] = v;
}

struct __PSStencil_kernel2 
{
  __PSDomain dom;
  PSGrid3DFloat g;
  int __g_index;
}
;

static struct __PSStencil_kernel2 __PSStencilMap_kernel2(__PSDomain dom,PSGrid3DFloat g)
/* Generated by generateMap */
{
  struct __PSStencil_kernel2 stencil = {dom, g, __PSGridGetID(g)};
  return stencil;
}

static void __PSStencilRun_kernel2(struct __PSStencil_kernel2 *s)
/* Generated by generateRunKernelBody */
{
  unsigned int i2;
  for (i2 = s -> dom.local_min[2]; i2 < s -> dom.local_max[2]; ++i2) {
    unsigned int i1;
    for (i1 = s -> dom.local_min[1]; i1 < s -> dom.local_max[1]; ++i1) {
      unsigned int i0;
      for (i0 = s -> dom.local_min[0]; i0 < s -> dom.local_max[0]; ++i0) {
        kernel2(i0,i1,i2,s -> g);
      }
    }
  }
}

void init(float *buff,const int dx,const int dy,const int dz)
{
  int jx;
  int jy;
  int jz;
  for (jz = 0; jz < dz; jz++) {
    for (jy = 0; jy < dy; jy++) {
      for (jx = 0; jx < dx; jx++) {
        int j = ((((jz * dx) * dy) + (jy * dx)) + jx);
        buff[j] = (((jx * jy) * jz) + 10);
      }
    }
  }
}
/* Generated by generateRun */

static void __PSStencilRun_0(int iter,struct __PSStencil_kernel1 s0)
/* Generated by generateRunBody */
{
  int i;
  __PSTraceStencilPre("kernel1");
  __PSStopwatch st;
  __PSStopwatchStart(&st);
  for (i = 0; i < iter; ++i) {
    __PSStencilRun_kernel1(&s0);
    __PSGridSwap(s0.g);
  }
  __PSTraceStencilPost(__PSStopwatchStop(&st));
}
/* Generated by generateRun */

static void __PSStencilRun_1(int iter,struct __PSStencil_kernel2 s0)
/* Generated by generateRunBody */
{
  int i;
  __PSTraceStencilPre("kernel2");
  __PSStopwatch st;
  __PSStopwatchStart(&st);
  for (i = 0; i < iter; ++i) {
    __PSStencilRun_kernel2(&s0);
    __PSGridSwap(s0.g);
  }
  __PSTraceStencilPost(__PSStopwatchStop(&st));
}

int main(int argc,char **argv)
{
  PSInit(&argc,&argv,3,64,64,64);
  PSGrid3DFloat g1;
{
    int dims[3] = {((index_t )64), ((index_t )64), ((index_t )64)};
    g1 = __PSGridNew(sizeof(float ),3,dims,0);
  }
  PSDomain3D d1 = PSDomain3DNew(0L,64L,0L,64L,0L,64L);
  int nx = 64;
  int ny = 64;
  int nz = 64;
  float *buff1 = (float *)(malloc(((((sizeof(float )) * nx) * ny) * nz)));
  float *buff2 = (float *)(malloc(((((sizeof(float )) * nx) * ny) * nz)));
  init(buff1,nx,ny,nz);
  PSGridCopyin(g1,buff1);
  PSGridCopyout(g1,buff2);
  size_t nelms = ((nx * ny) * nz);
  unsigned int i;
  for (i = 0; i < nelms; i++) {
    if (buff1[i] == buff2[i]) 
      continue; 
    fprintf(stderr,"Error: buff 1 and 2 differ at %i: %10.3f and %10.3f\n",i,buff1[i],buff2[i]);
  }
  PSGridCopyin(g1,buff1);
  __PSStencilRun_0(1,__PSStencilMap_kernel1(d1,g1));
  PSGridCopyout(g1,buff2);
  for (i = 0; i < nelms; i++) {
    if ((buff1[i] * 2) == buff2[i]) {
      continue; 
    }
    else {
      fprintf(stderr,"Error: buff 1 and 2 differ at %i: %10.3f and %10.3f\n",i,buff1[i],buff2[i]);
    }
  }
  PSGridCopyin(g1,buff1);
  __PSStencilRun_1(1,__PSStencilMap_kernel2(d1,g1));
  PSGridCopyout(g1,buff2);
  for (i = 0; i < nelms; i++) {
    if ((buff1[i] + 10) == buff2[i]) {
      continue; 
    }
    else {
      fprintf(stderr,"Error: buff 1 and 2 differ at %i: %10.3f and %10.3f\n",i,buff1[i],buff2[i]);
    }
  }
  free(buff1);
  free(buff2);
  PSGridFree(g1);
  PSFinalize();
  return 0;
}
