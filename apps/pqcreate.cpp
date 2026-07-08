/**
 * @file pqcreate.cpp
 * @brief Modernized utility to create an LDM product-queue.
 * @author Robert Toomey
 * @date May 2026
 */
#include "config.h"
#include "Application.h"
#include "Log.h"
#include "IProductStore.h"
#include "StorageFactory.h"
#include "Registry.h"
#include "NetworkFactory.h"
#include <iostream>
#include <string>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cerrno>

using namespace rdm;

class PqCreateApp : public Application {
private:
    std::string queuePath_;
    int pflags_{PqFlags::NoClobber};
    off_t initialsz_{0};
    size_t nproducts_{0};

protected:
    void ConfigureOptions() override {
        Application::ConfigureOptions();
        
        RegisterFlag('c', "Clobber existing product-queue if it exists");
        RegisterFlag('f', "Fast creation. Won't fill-in file blocks (Sparse)");
        RegisterOption('q', "pqfname", "Path to product queue file", "");
        RegisterOption('s', "size", "Maximum number of bytes to hold (e.g., 500m)", "");
        RegisterOption('S', "slots", "Maximum number of products to hold", "");
    }

    bool ProcessOptions() override {
        if (!Application::ProcessOptions()) return false;

        if (IsSet('c')) pflags_ &= ~PqFlags::NoClobber;
        if (IsSet('f')) pflags_ |= PqFlags::Sparse;

        std::string sizeStr = GetOption('s');
        queuePath_ = GetOption('q');

        // Robust Positional Argument Parsing
        if (positionalArgs_.size() == 2) {
            if (IsSet('s') || IsSet('q')) {
                LogError("Cannot mix exact flags (-s, -q) with full positional arguments (size path)");
                return false;
            }
            sizeStr = positionalArgs_[0];
            queuePath_ = positionalArgs_[1];
        } else if (positionalArgs_.size() == 1) {
            if (!IsSet('s')) {
                sizeStr = positionalArgs_[0];
            } else if (!IsSet('q')) {
                queuePath_ = positionalArgs_[0];
            } else {
                LogError("Too many arguments provided.");
                return false;
            }
        } else if (positionalArgs_.size() > 2) {
            LogError("Too many positional arguments provided.");
            return false;
        }

        // Set the registry if a queue path was requested
        if (!queuePath_.empty()) {
            registry::setQueuePath(queuePath_);
        }
        queuePath_ = registry::getQueuePath();

        // Size String Parsing logic
        if (sizeStr.empty()) {
            LogError("No size specified. A queue size is required.");
            return false;
        }

        char* cp;
        errno = 0;
        initialsz_ = std::strtol(sizeStr.c_str(), &cp, 0);
        if (errno != 0) {
            initialsz_ = 0;
        } else {
            int exponent = 0;
            switch (*cp) {
                case 'k': case 'K': exponent = 1; break;
                case 'm': case 'M': exponent = 2; break;
                case 'g': case 'G': exponent = 3; break;
            }
            if (initialsz_ > 0) {
                // The original pqcreate.cpp multiplies by 1000, not 1024
                for (int i = 0; i < exponent; i++) {
                    initialsz_ *= 1000;
                    if (initialsz_ <= 0) {
                        LogError("Size \"{}\" too big (overflow)", sizeStr);
                        return false;
                    }
                }
            }
        }

        if (initialsz_ <= 0) {
            LogError("Illegal size \"{}\"", sizeStr);
            return false;
        }

        if (IsSet('S')) {
            nproducts_ = static_cast<size_t>(std::atol(GetOption('S').c_str()));
            if (nproducts_ == 0) {
                LogError("Illegal nproducts \"{}\"", GetOption('S'));
                return false;
            }
        } else {
            // Default slot calculation based on an average product size of 140,000 bytes
            #define PQ_AVG_PRODUCT_SIZE 140000
            nproducts_ = initialsz_ / PQ_AVG_PRODUCT_SIZE;
        }

        return true;
    }

    int Run() override {
        LogInfo("Creating {}, {} bytes, {} products.", queuePath_, static_cast<long>(initialsz_), static_cast<long>(nproducts_));
        
        auto serializer = NetworkFactory::CreateSerializer();
        std::unique_ptr<IProductStore> pq = StorageFactory::Create(serializer);
        
        int errnum = pq->create(queuePath_.c_str(), 0666, pflags_, 0, initialsz_, nproducts_);
        if (errnum) {
            LogError("create \"{}\" failed: {}", queuePath_, std::strerror(errnum));
            return EXIT_FAILURE;
        }
        
        pq->close();
        return EXIT_SUCCESS;
    }

public:
    PqCreateApp() : Application("Creates and allocates an LDM product queue.") {}
};

int main(int argc, char *argv[]) {
    PqCreateApp app;
    return app.Execute(argc, argv);
}
