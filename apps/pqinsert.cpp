/**
 * @file pqinsert.cpp
 * @brief Modernized utility to insert files as products into an LDM product queue.
 * @author Robert Toomey
 * @date May 2026
 */
#include "config.h"
#include "QueueApp.h"
#include "Log.h"
#include "NetworkUtils.h"

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <csignal>

using namespace rdm;

enum ExitCodes {
    EXIT_SUCCESS_PQ   = 0,
    EXIT_OS_ERROR     = 1,
    EXIT_QUEUE_ERROR  = 2,
    EXIT_FILE_ERROR   = 3,
    EXIT_DUP_ERROR    = 4
};

class PqInsertApp : public QueueApp {
private:
    FeedType feed_type_{ANY};
    std::string custom_prod_id_{""};
    int sequence_number_{0};
    bool verbose_{false};
    bool signature_from_ident_{false};

protected:
    void ConfigureOptions() override {
        QueueApp::ConfigureOptions();
        
        RegisterFlag('i', "Compute MD5 signature from product-identifier instead of data");
        RegisterOption('f', "feed", "Set product feed type (default: ANY)", "ANY");
        RegisterOption('p', "id", "Override default product identifier", "");
        RegisterOption('s', "seq", "Starting sequence number (default: 0)", "0");
    }

    bool ProcessOptions() override {
        if (!QueueApp::ProcessOptions()) return false;
        
        verbose_ = IsSet('v');
        signature_from_ident_ = IsSet('i');
        
        if (IsSet('f')) {
            if (FeedType::Parse(GetOption('f'), feed_type_) != FEEDTYPE_OK) {
                LogError("Invalid feedtype target: {}", GetOption('f'));
                return false;
            }
        }
        if (IsSet('p')) custom_prod_id_ = GetOption('p');
        if (IsSet('s')) sequence_number_ = std::stoi(GetOption('s'));
        
        if (positionalArgs_.empty()) {
            LogError("Missing file payload argument(s). You must specify at least one file to insert.");
            return false;
        }
        return true;
    }

    int Run() override {
        std::string myname = network::GetLocalHostName();
        int final_exit_status = EXIT_SUCCESS_PQ;
        bool multiple_files = positionalArgs_.size() > 1;

        // Iterate over the trailing positional arguments natively captured by the base class
        for (const auto& filename : positionalArgs_) {
            std::ifstream file(filename, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                LogError("Failed to open source payload file: {}", filename);
                final_exit_status = EXIT_FILE_ERROR;
                continue;
            }

            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            if (size < 0) {
                LogError("Unreadable file stream: {}", filename);
                file.close();
                continue;
            }

            std::vector<char> buffer;
            if (size > 0) {
                buffer.resize(size);
                if (!file.read(buffer.data(), size)) {
                    LogError("Error reading binary frames from: {}", filename);
                    final_exit_status = EXIT_FILE_ERROR;
                    continue;
                }
            }
            file.close();

            Product prod;
            prod.info.origin = myname;
            prod.info.feedtype = feed_type_;
            prod.info.seqno = sequence_number_++;
            prod.info.arrival = Timestamp::Now();

            std::string prod_id = custom_prod_id_.empty() ? filename : custom_prod_id_;
            if (!custom_prod_id_.empty() && multiple_files) {
                // If a custom ID is provided but we are looping over multiple files,
                // we must append the sequence number to avoid identical IDs.
                prod_id += "." + std::to_string(prod.info.seqno);
            }
            
            prod.info.ident = prod_id;
            prod.info.sz = static_cast<unsigned int>(size);
            prod.data = (size > 0) ? reinterpret_cast<const uint8_t*>(buffer.data()) : nullptr;

            if (signature_from_ident_) {
                prod.info.signature = Signature::GenerateMD5(prod.info.ident.c_str(), 
                  prod.info.ident.length());
            } else {
                prod.info.signature = Signature::GenerateMD5(prod.data, prod.info.sz);
            }

            int status = pq_->insert(prod);
            if (status == 0) {
                // Signal local daemon process group that a product was inserted
                (void)kill(0, SIGCONT);

                if (verbose_) {
                    LogInfo("Successfully inserted product: ID={}, Size={} bytes", prod.info.ident, prod.info.sz);
                }
            } else if (status == static_cast<int>(PqStatus::Dup)) {
                LogWarning("Product rejected: Duplicate MD5 signature matches an existing entry ({})", filename);
                if (final_exit_status == EXIT_SUCCESS_PQ) final_exit_status = EXIT_DUP_ERROR;
            } else {
                LogError("Queue write error on item {}: {}", filename, pq_->strerror(status));
                final_exit_status = EXIT_QUEUE_ERROR;
            }
        }
        return final_exit_status;
    }

public:
    // Initialize the QueueApp with PqFlags::Default to ensure write-access
    PqInsertApp() : QueueApp(PqFlags::Default, "Inserts one or more files into an LDM product queue.") {}
};

int main(int argc, char *argv[]) {
    PqInsertApp app;
    return app.Execute(argc, argv);
}
