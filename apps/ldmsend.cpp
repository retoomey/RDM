#include "config.h"
#include "Application.h"
#include "Log.h"
#include "NetworkFactory.h"
#include "NetworkUtils.h"
#include "FileUtil.h"
#include "Signature.h"
#include "Timestamp.h"
#include "FeedType.h"
#include "ServiceAddr.h"
#include "ProdClass.h"
#include "IClient.h"

#include <iostream>
#include <vector>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <cstring>

using namespace rdm;

class LdmSendApp : public Application {
private:
    std::string remoteHost_{"localhost"};
    unsigned int port_{388};
    FeedType feedtype_{EXP};
    uint32_t seqStart_{0};
    
    int ProcessFile(IClient* client, const std::string& filename, uint32_t& seqno, const ProdClass& wantClass) {
        int fd = ::open(filename.c_str(), O_RDONLY, 0);
        if (fd == -1) {
            LogSyserr("open: {}", filename);
            return -1;
        }

        struct stat statb;
        if (::fstat(fd, &statb) == -1) {
            LogSyserr("fstat: {}", filename);
            ::close(fd);
            return -1;
        }

        LogInfo("Sending {}, {} bytes", filename, statb.st_size);

        void* mappedPtr = nullptr;
        int status = mapwrap(fd, 0, statb.st_size, PROT_READ, MAP_PRIVATE, &mappedPtr);
        if (status != 0 || mappedPtr == nullptr) {
            LogSyserr("Couldn't memory-map file: {}", filename);
            ::close(fd);
            return -1;
        }

        // Construct the modern product representation 
        // Note: Legacy ldmsend uses the full passed filename string as the ident
        Product prod;
        prod.info.ident = filename; 
        prod.info.origin = network::GetLocalHostName();
        prod.info.feedtype = feedtype_;
        prod.info.seqno = seqno++;
        prod.info.arrival = Timestamp::Now();
        prod.info.sz = static_cast<uint32_t>(statb.st_size);
        prod.info.signature = Signature::GenerateMD5(mappedPtr, statb.st_size);
        prod.data = static_cast<const uint8_t*>(mappedPtr);

        // Client-side verification against the negotiated Server Want class
        if (!wantClass.Contains(prod.info)) {
            LogWarning("{} doesn't strictly want {}, but sending anyway to delegate to server logic", remoteHost_, filename);
        }

        // Transmit via modern client interface
        status = client->SendProduct(prod);
        if (status != 0) {
            LogError("Couldn't send file \"{}\" to LDM: {}", filename, client->GetLastError());
        }

        unmapwrap(mappedPtr, 0, statb.st_size, 0);
        ::close(fd);

        return status;
    }

protected:
    void ConfigureOptions() override {
        Application::ConfigureOptions();
        RegisterOption('h', "remote", "Remote service host", "localhost");
        RegisterOption('P', "port", "The port on the remote system", "388");
        RegisterOption('s', "seqno", "Initial product sequence number", "0");
        RegisterOption('f', "feedtype", "Assert data feed type", "EXP");
    }

    bool ProcessOptions() override {
        if (!Application::ProcessOptions()) return false;

        remoteHost_ = GetOption('h');
        port_ = static_cast<unsigned int>(std::stoul(GetOption('P')));
        seqStart_ = static_cast<uint32_t>(std::stoul(GetOption('s')));

        if (FeedType::Parse(GetOption('f'), feedtype_) != FEEDTYPE_OK) {
            LogError("Unknown feedtype \"{}\"", GetOption('f'));
            return false;
        }

        if (positionalArgs_.empty()) {
            LogError("At least one filename must be specified.");
            return false;
        }

        return true;
    }

    int Run() override {
        // Construct target endpoint
        auto targetOpt = ServiceAddr::Parse(remoteHost_, "localhost", port_);
        if (!targetOpt) {
            LogError("Invalid remote host address: {}", remoteHost_);
            return EXIT_FAILURE;
        }

        // Initialize client connection
        auto client = NetworkFactory::CreateClient(*targetOpt, 60);
        if (!client) {
            LogError("Failed to create network client for {}", targetOpt->ToString());
            return EXIT_FAILURE;
        }

        if (client->Connect() != 0) {
            LogError("Failed to connect to {}: {}", targetOpt->ToString(), client->GetLastError());
            return EXIT_FAILURE;
        }

        // Establish session offer using HiyaRequest/HiyaResponse
        HiyaRequest req;
        req.offeredClass.from_sec = Timestamp::Now().tv_sec;
        req.offeredClass.from_usec = Timestamp::Now().tv_usec;
        req.offeredClass.to_sec = Timestamp::ENDT.tv_sec;
        req.offeredClass.to_usec = Timestamp::ENDT.tv_usec;
        req.offeredClass.specs.push_back({feedtype_, ".*"});

        HiyaResponse resp = client->SendHiya(req, 60);
        if (resp.statusCode != ReplyStatus::OK) {
            LogError("HIYA negotiation failed with status {}", static_cast<int>(resp.statusCode));
            return EXIT_FAILURE;
        }

        // Set max hereis bounds negotiated from the server
        client->SetMaxHereIs(resp.maxHereis);

        // Process files sequentially
        int overallStatus = EXIT_SUCCESS;
        uint32_t currentSeq = seqStart_;

        for (const auto& filename : positionalArgs_) {
            if (SignalManager::IsDone()) break;

            if (ProcessFile(client.get(), filename, currentSeq, resp.acceptedClass) != 0) {
                overallStatus = EXIT_FAILURE;
                break;
            }
        }

        client->Flush();
        client->Disconnect();
        return overallStatus;
    }

public:
    LdmSendApp() : Application("LDM data source example client") {}
};

int main(int argc, char* argv[]) {
    LdmSendApp app;
    return app.Execute(argc, argv);
}
