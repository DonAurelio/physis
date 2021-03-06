// Licensed under the BSD license. See LICENSE.txt for more details.

#include "runtime/grid_mpi.h"

#include <limits.h>
#include <algorithm>

#include "runtime/grid_util.h"

using namespace std;

namespace physis {
namespace runtime {

size_t GridMPI::CalcHaloSize(int dim, unsigned width, bool diagonal) {
  // This class sends the whole allocated halo region, irrespective of
  // how much of it is actually required, which is specified by
  // width. This avoids non-continuous memory copies and simplifies
  // implementations.
  IndexArray halo_size = local_real_size_;
  halo_size[dim] = width;
  return halo_size.accumulate(num_dims_);
}

GridMPI::GridMPI(const __PSGridTypeInfo *type_info,
                 int num_dims, const IndexArray &size,
                 const IndexArray &global_offset,
                 const IndexArray &local_offset,                 
                 const IndexArray &local_size,
                 const Width2 &halo, const Width2 *halo_member,
                 int attr):
    Grid(type_info, num_dims, size, attr),
    global_offset_(global_offset),  
    local_offset_(local_offset), local_size_(local_size),
    halo_(halo), halo_member_(NULL), halo_self_fw_(NULL), halo_self_bw_(NULL),
  halo_peer_fw_(NULL), halo_peer_bw_(NULL) {

  empty_ = local_size_.accumulate(num_dims_) == 0;
  if (empty_) return;
  
  local_real_size_ = local_size_;
  local_real_offset_ = local_offset_;
  for (int i = 0; i < num_dims_; ++i) {
    local_real_size_[i] += halo.fw[i] + halo.bw[i];
    local_real_offset_[i] -= halo.bw[i];
  }

  if (type_info->num_members > 0) {
    halo_member_ = new Width2[type_info->num_members];
    for (int i = 0; i < type_info->num_members; ++i) {
      halo_member_[i] = halo_member[i];
    }
  }
  
  // Hanlde primitive type as a single-member user-defined type
  if (type_ != PS_USER) {
    type_info_.num_members = 1;
    type_info_.members = new __PSGridTypeMemberInfo;
    type_info_.members->type = type_info_.type;
    type_info_.members->size = type_info_.size;
    type_info_.members->rank = 0;
    PSAssert(halo_member_ == NULL);
    halo_member_ = new Width2[1];
    halo_member_[0] = halo_;
  } else {
    // Aggregates multiple members in MPI since AoS is not converted
    // to SoA
    type_info_.num_members = 1;
    type_info_.members[0].size = type_info_.size;
    type_info_.members[0].type = PS_USER;
    halo_member_[0] = halo_;
  }
  
}

GridMPI *GridMPI::Create(PSType type, int elm_size,
                         int num_dims, const IndexArray &size,
                         const IndexArray &global_offset,
                         const IndexArray &local_offset,
                         const IndexArray &local_size,
                         const Width2 &halo,
                         int attr) {
  __PSGridTypeInfo info = {type, elm_size, 0, NULL};
  return GridMPI::Create(&info, num_dims, size, global_offset,
                         local_offset, local_size, halo, NULL, attr);
}

GridMPI* GridMPI::Create(const __PSGridTypeInfo *type_info,
                         int num_dims, const IndexArray &size,
                         const IndexArray &global_offset,
                         const IndexArray &local_offset,
                         const IndexArray &local_size,
                         const Width2 &halo,
                         const Width2 *halo_member,                         
                         int attr) {
  GridMPI *g = new GridMPI(type_info, num_dims, size,
                           global_offset, local_offset,
                           local_size, halo, halo_member, attr);
  g->InitBuffers();
  return g;
}


void GridMPI::InitBuffers() {
  if (empty_) return;  
  data_buffer_ = new BufferHost();
  data_buffer_->Allocate(GetLocalBufferRealSize());
  //data_ = (char*)data_buffer_->Get();
  //LOG_DEBUG() << "buffer addr: " << (void*)data_ << "\n";
  InitHaloBuffers();
}

void GridMPI::InitHaloBuffers() {
  // Note that the halo for the last dimension is continuously located
  // in memory, so no separate buffer is necessary.

  halo_self_fw_ = new char*[num_dims_];
  halo_self_bw_ = new char*[num_dims_];
  halo_peer_fw_ = new char*[num_dims_];
  halo_peer_bw_ = new char*[num_dims_];
  
  for (int i = 0; i < num_dims_ - 1; ++i) {
    // Initialize to NULL by default
    halo_self_fw_[i] = halo_self_bw_[i] = NULL;
    halo_peer_fw_[i] = halo_peer_bw_[i] = NULL;
    if (halo_.fw[i]) {
      halo_self_fw_[i] =
          (char*)malloc(CalcHaloSize(i, halo_.fw[i], true) * elm_size());
      assert(halo_self_fw_[i]);
      halo_peer_fw_[i] =
          (char*)malloc(CalcHaloSize(i, halo_.fw[i], true) * elm_size());
      assert(halo_peer_fw_[i]);      
    } 
    if (halo_.bw[i]) {
      halo_self_bw_[i] =
          (char*)malloc(CalcHaloSize(i, halo_.bw[i], true) * elm_size());
      assert(halo_self_bw_[i]);
      halo_peer_bw_[i] =
          (char*)malloc(CalcHaloSize(i, halo_.bw[i], true) * elm_size());
      assert(halo_peer_bw_[i]);      
    } 
  }
}

GridMPI::~GridMPI() {
  delete[] halo_member_;
  DeleteBuffers();
}

void GridMPI::DeleteBuffers() {
  if (empty_) return;
  DeleteHaloBuffers();
  Grid::DeleteBuffers();
}

void GridMPI::DeleteHaloBuffers() {
  if (empty_) return;
  
  for (int i = 0; i < num_dims_ - 1; ++i) {
    if (halo_self_fw_) PS_XFREE(halo_self_fw_[i]);
    if (halo_self_bw_) PS_XFREE(halo_self_bw_[i]);
    if (halo_peer_fw_) PS_XFREE(halo_peer_fw_[i]);
    if (halo_peer_bw_) PS_XFREE(halo_peer_bw_[i]);
  }
  PS_XDELETEA(halo_self_fw_);
  PS_XDELETEA(halo_self_bw_);
  PS_XDELETEA(halo_peer_fw_);
  PS_XDELETEA(halo_peer_bw_);
}

char *GridMPI::GetHaloPeerBuf(int dim, bool fw, unsigned width) {
  if (dim == num_dims_ - 1) {
    IndexArray offset(0);
    if (fw) {
      offset[dim] = local_real_size_[dim] - halo_.fw[dim];
    } else {
      offset[dim] = halo_.bw[dim] - width;
    }
    return (char*)(idata()
                   + GridCalcOffset(offset, local_real_size_, num_dims_)
                   * elm_size());
  } else {
    if (fw) return halo_peer_fw_[dim];
    else  return halo_peer_bw_[dim];
  }
  
}
// fw: copy in halo buffer received for forward access if true
void GridMPI::CopyinHalo(int dim, const Width2 &width, bool fw, bool diagonal) {
  // The slowest changing dimension does not need actual copying
  // because it's directly copied into the grid buffer.
  if (dim == num_dims_ - 1) {
    return;
  }
  
  IndexArray halo_offset(0);
  if (fw) {
    halo_offset[dim] = local_real_size_[dim] - halo_.fw[dim];
  } else {
    halo_offset[dim] = halo_.bw[dim] - width(fw)[dim];
  }
  
  char *halo_buf = fw ? halo_peer_fw_[dim] : halo_peer_bw_[dim];

  IndexArray halo_size = local_real_size_;
  halo_size[dim] = width(fw)[dim];
  
  CopyinSubgrid(elm_size(), num_dims_, data(), local_real_size_,
                halo_buf, halo_offset, halo_size);
}

// fw: prepare buffer for sending halo for forward access if true
void GridMPI::CopyoutHalo(int dim, const Width2 &width, bool fw, bool diagonal) {
#if 0
  LOG_DEBUG() << "FW?: " << fw << ", width: " << width
              << ", local size: " << local_size_
              << ", halo fw: " << halo_.fw
              << ", halo bw: " << halo_.bw << "\n";
#endif

  if (halo_(dim, fw) == 0) {
    LOG_DEBUG() << "No " << (fw ? "forward" : "backward")
                << " halo for dimension " << dim << "\n";
    return;
  }
  
  IndexArray halo_offset(0);
  if (fw) {
    halo_offset[dim] = halo_.bw[dim];
  } else {
    halo_offset[dim] = local_real_size_[dim] - halo_.fw[dim] - width.bw[dim];
  }

  LOG_DEBUG() << "halo offset: " << halo_offset << "\n";
  
  char *&halo_buf = GetHaloSelf(dim, fw);

  // The slowest changing dimension does not need actual copying
  // because its halo region is physically continuous.
  if (dim == (num_dims_ - 1)) {
    char *p = (char*)(idata()
                      + GridCalcOffset(halo_offset, local_real_size_, num_dims_)
                      * elm_size());
    halo_buf = p;
    return;
  } else {
    IndexArray halo_size = local_real_size_;
    halo_size[dim] = width(fw)[dim];
    CopyoutSubgrid(elm_size(), num_dims_, data(), local_real_size_,
                   halo_buf, halo_offset, halo_size);
    return;
  }
}


std::ostream &GridMPI::Print(std::ostream &os) const {
  os << "GridMPI {"
     << "type: " << ToString(type_)
     << ", num_dims: " << num_dims_
     << "elm_size: " << elm_size()
     << ", size: " << size_
     << ", global offset: " << global_offset_
     << ", local offset: " << local_offset_
     << ", local size: " << local_size_
     << ", local real size: " << local_real_size_
     << ", halo: " << halo_
     << "}";
  return os;
}

template <class T>
int ReduceGridMPI(GridMPI *g, PSReduceOp op, T *out, int dim) {
  PSAssert(dim <= 3);
  size_t nelms = g->local_size().accumulate(g->num_dims());
  if (nelms == 0) return 0;
  boost::function<T (T, T)> func = GetReducer<T>(op);
  T *d = (T *)g->data();
  T v = GetReductionDefaultValue<T>(op);
  int imax = g->local_size()[0];
  int jmax = g->local_size()[1];
  int kmax = g->local_size()[2];
  if (dim == 1) {
    jmax = kmax = 1;
  } else if (dim == 2) {
    kmax = 1;
  }
  for (int k = 0; k < kmax; ++k) {
    for (int j = 0; j < jmax; ++j) {
      for (int i = 0; i < imax; ++i) {
        IndexArray p(i, j, k);
        p += g->halo().bw;
        intptr_t offset =
            GridCalcOffset(p, g->local_real_size(), dim);
        v = func(v, d[offset]);
      }
    }
  }
  *out = v;
  return nelms;
}

int GridMPI::Reduce(PSReduceOp op, void *out) {
  int rv = 0;
  PSAssert(num_dims_ <= 3);
  switch (type_) {
    case PS_FLOAT:
      rv = ReduceGridMPI<float>(this, op, (float*)out, num_dims_);
      break;
    case PS_DOUBLE:
      rv = ReduceGridMPI<double>(this, op, (double*)out, num_dims_);
      break;
    case PS_INT:
      rv = ReduceGridMPI<int>(this, op, (int*)out, num_dims_);
      break;
    case PS_LONG:
      rv = ReduceGridMPI<long>(this, op, (long*)out, num_dims_);
      break;
    default:
      LOG_ERROR() << "Unsupported type\n";
      PSAbort(1);
  }
  return rv;
}


void GridMPI::Copyout(void *dst)  {
  const void *src = buffer()->Get();
  if (HasHalo()) {
    IndexArray offset(halo_.bw);
    CopyoutSubgrid(elm_size(), num_dims(),
                   src, local_real_size(),
                   dst, offset, local_size());
  } else {
    memcpy(dst, src, GetLocalBufferSize());
  }
  return;
}

void GridMPI::Copyin(const void *src) {
  void *dst = buffer()->Get();
  if (HasHalo()) {
    CopyinSubgrid(elm_size(), num_dims(),
                  dst, local_real_size(),
                  src, halo_.bw, local_size());
  } else {
    memcpy(dst, src, GetLocalBufferSize());
  }
  return;
}

} // namespace runtime
} // namespace physis


