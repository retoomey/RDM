#include "ProductQueue.h"
#include "IProductSerializer.h"
#include <memory>

using namespace rdm;

// Disable name mangling so StorageFactory can find this exact symbol via dlsym.
// Note: Passing a std::shared_ptr through extern "C" is safe here because both 
// the core and the plugin are compiled by the same compiler (ABI consistency).
extern "C" {
    IProductStore* create_store(std::shared_ptr<IProductSerializer> serializer) {
        return new ProductQueue(std::move(serializer));
    }
}
