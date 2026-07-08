#include "FileAction.h"
#include "UnioEntry.h"
#include "FileCache.h"
#include "FileOpsUtil.h"
#include "PqactContext.h"
#include "Log.h"
#include <unistd.h>

namespace rdm {
namespace pqact {

int FileAction::Execute(const Product& prod, const std::vector<std::string>& args, const void* xprod, size_t xlen) {
    if (args.empty()) return -1;

    bool isNew = false;
    UnioEntry* entry = context_.fileCache->GetOrCreate<UnioEntry>(args, isNew);
    if (!entry) {
        LogError("Couldn't get entry for product \"{}\"", prod.info.ident);
        return -1;
    }

    // --- REFACTORED PAYLOAD PREP ---
    std::vector<uint8_t> strippedData;
    pqact::BufferView view = FileOpsUtil::PreparePayload(prod, entry->GetFlags(), strippedData);

    if (entry->IsFlagSet(FL_OVERWRITE)) {
        if (lseek(entry->GetFd(), 0, SEEK_SET) < 0) {
            LogSyserr("Couldn't seek to beginning of file {}", entry->GetPath().c_str());
        }
    }

    int status = entry->Write(prod, view.data, view.size);

    if (status == 0) {
        if (entry->IsFlagSet(FL_OVERWRITE)) {
            const off_t fileSize = lseek(entry->GetFd(), 0, SEEK_CUR);
            if (fileSize != static_cast<off_t>(-1)) {
                (void) ftruncate(entry->GetFd(), fileSize);
            }
        }
        if (entry->IsFlagSet(FL_FLUSH)) {
            status = entry->Sync(true);
            if (status){
              LogError("Couldn't flush I/O to file \"{}\"", entry->GetPath());
            }
        }
        if (status == 0 && entry->IsFlagSet(FL_LOG) && log_is_enabled_notice) {
            LogNotice("Filed in \"{}\": {}", entry->GetPath(), prod.info.ToString(log_is_enabled_debug));
        }
    }

    if (status != 0 || entry->IsFlagSet(FL_CLOSE)) {
        context_.fileCache->RemoveAndFree(entry);
    }

    return status ? -1 : 0;
}

} // namespace pqact
}
