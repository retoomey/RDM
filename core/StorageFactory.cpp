#include "StorageFactory.h"
#include "Registry.h"
#include "Log.h"
#include <mutex>
#include <cctype>
#include <dlfcn.h>

namespace rdm {

struct StoragePlugin {
    void* handle{nullptr};
    IProductStore* (*create_store)(std::shared_ptr<IProductSerializer>){nullptr};
};

static StoragePlugin current_plugin;
static std::mutex plugin_mutex;

static void LoadPlugin(const std::string& type) {
    std::lock_guard<std::mutex> lock(plugin_mutex);
    if (current_plugin.handle) return;

    std::string libName = "librdmstore" + type + ".so";
    void* handle = dlopen(libName.c_str(), RTLD_NOW | RTLD_LOCAL);
    
    if (!handle) {
        libName = "lib" + type + ".so";
        handle = dlopen(libName.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            LogFatal("Failed to load storage engine plugin '{}': {}", type, dlerror());
            exit(1);
        }
    }

    current_plugin.create_store = reinterpret_cast<IProductStore*(*)(std::shared_ptr<IProductSerializer>)>(dlsym(handle, "rdm_create_store"));

    if (!current_plugin.create_store) {
        LogFatal("Plugin '{}' is missing required exported C-symbols: {}", type, dlerror());
        dlclose(handle);
        exit(1);
    }
    
    current_plugin.handle = handle;
}

std::string StorageFactory::GetEngineType() {
    std::string engine = registry::getString(registry::RegistryKey::StorageEngine);
    for (char& c : engine) c = std::tolower(c);
    
    // --- SECURITY BOUNDARY: STRICT PLUGIN WHITELIST ---
    if (engine == "pq") {
        return engine;
    }
    // Future engines (e.g., "redis", "ringbuffer") must be explicitly authorized here.

    LogFatal("Security Violation: Invalid or unauthorized storage engine '{}' requested.", engine);
    exit(1);
}

std::unique_ptr<IProductStore> StorageFactory::Create(std::shared_ptr<IProductSerializer> serializer) {
    LoadPlugin(GetEngineType());
    return std::unique_ptr<IProductStore>(current_plugin.create_store(std::move(serializer)));
}

}
