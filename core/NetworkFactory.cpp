#include "NetworkFactory.h"
#include "Registry.h"
#include "Log.h"

#include <mutex>
#include <cctype>

#include <dlfcn.h>

namespace rdm {

struct EnginePlugin {
    void* handle{nullptr};
    IServer* (*create_server)(){nullptr};
    IClient* (*create_client)(const char*, uint16_t, unsigned int){nullptr};
    IClient* (*create_client_handoff)(const char*, uint16_t, int, const struct sockaddr_storage*, unsigned int){nullptr};
    IProductSerializer* (*create_serializer)(){nullptr};
};

static EnginePlugin current_plugin;
static std::mutex plugin_mutex;

static void LoadPlugin(const std::string& type) {
    std::lock_guard<std::mutex> lock(plugin_mutex);
    if (current_plugin.handle) return;

    // e.g., "librdmnetsunrpc.so"
    std::string libName = "librdmnet" + type + ".so";
    void* handle = dlopen(libName.c_str(), RTLD_NOW | RTLD_GLOBAL);
    
    if (!handle) {
        // Fallback: try without the rdm prefix
        libName = "lib" + type + ".so";
        handle = dlopen(libName.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
            LogFatal("Failed to load network engine plugin '{}': {}", type, dlerror());
            exit(1);
        }
    }

    current_plugin.create_server = reinterpret_cast<IServer*(*)()>(dlsym(handle, "create_server"));
    current_plugin.create_client = reinterpret_cast<IClient*(*)(const char*, uint16_t, unsigned int)>(dlsym(handle, "create_client"));
    current_plugin.create_client_handoff = reinterpret_cast<IClient*(*)(const char*, uint16_t, int, const struct sockaddr_storage*, unsigned int)>(dlsym(handle, "create_client_handoff"));
    current_plugin.create_serializer = reinterpret_cast<IProductSerializer*(*)()>(dlsym(handle, "create_serializer"));

    if (!current_plugin.create_server || !current_plugin.create_client || 
        !current_plugin.create_client_handoff || !current_plugin.create_serializer) {
        LogFatal("Plugin '{}' is missing required exported C-symbols: {}", type, dlerror());
        dlclose(handle);
        exit(1);
    }
    
    current_plugin.handle = handle;
}

std::string NetworkFactory::GetEngineType() {
    std::string engine = registry::getString(registry::RegistryKey::NetworkEngine);
    for (char& c : engine) c = std::tolower(c);
    return engine;
}

std::unique_ptr<IServer> NetworkFactory::CreateServer() {
    LoadPlugin(GetEngineType());
    return std::unique_ptr<IServer>(current_plugin.create_server());
}

std::unique_ptr<IClient> NetworkFactory::CreateClient(ServiceAddr target, unsigned int timeout_sec) {
    LoadPlugin(GetEngineType());
    return std::unique_ptr<IClient>(current_plugin.create_client(target.GetHost().c_str(), target.GetPort(), timeout_sec));
}

std::unique_ptr<IClient> NetworkFactory::CreateClient(
    ServiceAddr target, int existing_socket, const struct sockaddr_storage* remote_addr, unsigned int timeout_sec) {
    LoadPlugin(GetEngineType());
    return std::unique_ptr<IClient>(current_plugin.create_client_handoff(
        target.GetHost().c_str(), target.GetPort(), existing_socket, remote_addr, timeout_sec));
}

std::shared_ptr<IProductSerializer> NetworkFactory::CreateSerializer() {
    LoadPlugin(GetEngineType());
    return std::shared_ptr<IProductSerializer>(current_plugin.create_serializer());
}

}
