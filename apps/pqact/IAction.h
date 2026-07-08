#pragma once

#include "Product.h"
#include <string>
#include <vector>

namespace rdm {
namespace pqact {
struct PqactContext;

class IAction {
protected:
  PqactContext& context_; // Every action gets access to the state!

public:
  explicit IAction(PqactContext& ctx) : context_(ctx){ }

  virtual
  ~IAction() = default;

  // The modern replacement for the old prod_action function pointer
  virtual int
  Execute(const Product& prod, const std::vector<std::string>& args, const void * xprod, size_t xlen) = 0;

  virtual const char *
  GetName() const = 0;
  virtual bool
  IsTransient() const { return false; }
};
} // namespace pqact
} 
