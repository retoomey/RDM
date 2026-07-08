#pragma once
#include "IProductStore.h"
#include <sys/time.h>
#include <sys/types.h>

namespace rdm {
class ProductQueue;
class QueueCursor : public IQueueCursor {
private:
  ProductQueue& pq_;
  struct timeval cursor_ { 0, 0 };
  off_t cursor_offset_{ -1 };

  void
  pq_cset(const struct timeval * tvp);
  void
  pq_coffset(off_t c_offset);
  int
  sequenceHelper(Match mt, const ProdClass& clss, pq_seqfunc * ifMatch, void * otherargs,
    off_t * const offset);

public:
  explicit
  QueueCursor(ProductQueue& pq);
  ~QueueCursor() override = default;

  void
  setCursor(const Timestamp& timestamp) override;
  void
  setCursorOffset(const off_t offset) override;
  void
  getCursorTimestamp(Timestamp& tv) const override;

  int
  setCursorClass(Match * mtp, const ProdClass& clss) override;
  int
  checkCursorTime(Match mt, const ProdClass& clss, const Timestamp& maxLatency) const override;
  int
  setCursorFromSignature(const Signature& signature) override;
  int
  setCursorToLast(const ProdClass& prodClass, Timestamp& outTimestamp) override;

  int
  next(const bool reverse, const ProdClass& prodClass, pq_next_func * const callback, const bool keepLocked,
    void * const appArgs) override;
  int
  sequence(Match mt, const ProdClass& clss, pq_seqfunc * ifMatch, void * otherargs) override;
  int
  sequenceDelete(Match mt, const ProdClass& clss, const int wait, size_t& extentp,
    Timestamp& timestampp) override;
};
}
