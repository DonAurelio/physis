// Licensed under the BSD license. See LICENSE.txt for more details.

#ifndef PHYSIS_RUNTIME_PROC_H_
#define PHYSIS_RUNTIME_PROC_H_

#include "runtime/runtime_common.h"
#include "runtime/ipc.h"

namespace physis {
namespace runtime {

class Proc {
 protected:
  int rank_;
  int num_procs_;
  InterProcComm *ipc_;
  __PSStencilRunClientFunction *stencil_runs_;
 public:
  Proc(InterProcComm *ipc,
       __PSStencilRunClientFunction *stencil_runs):
      rank_(ipc->GetRank()), num_procs_(ipc->GetNumProcs()), ipc_(ipc),
      stencil_runs_(stencil_runs) {}
  virtual ~Proc() {}
  std::ostream &print(std::ostream &os) const;
  int rank() const { return rank_; }
  int num_procs() const { return num_procs_; }
  InterProcComm *ipc() { return ipc_; }  
  static int GetRootRank() { return 0; }
  bool IsRoot() const { return rank_ == GetRootRank(); }
};

} // namespace runtime
} // namespace physis

inline
std::ostream &operator<<(std::ostream &os, const physis::runtime::Proc &proc) {
  return proc.print(os);
}

#endif /* PHYSIS_RUNTIME_PROC_H_ */
