#pragma once
#include "IProductStore.h"
#include "IProductSerializer.h"
#include <memory>
#include <string>

namespace rdm {
class StorageFactory {
public:
  static std::unique_ptr<IProductStore>
  Create(std::shared_ptr<IProductSerializer> serializer);

private:
  static std::string
  GetEngineType();
};
}
