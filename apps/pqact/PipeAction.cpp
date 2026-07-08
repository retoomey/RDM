#include "PipeAction.h"
#include "PipeEntry.h"
#include "FileCache.h"
#include "FileOpsUtil.h"
#include "PqactContext.h"
#include "Log.h"

namespace rdm {
namespace pqact {

int PipeAction::Execute(const Product& prod, const std::vector<std::string>& args, const void* xprod, size_t xlen) {
    if (args.empty()) return -1;

    bool isNew = false;
    PipeEntry* entry = context_.fileCache->GetOrCreate<PipeEntry>(args, isNew);
    if (!entry) {
        LogError("Couldn't get entry for product \"{}\"", prod.info.ident);
        return -1;
    }
    
    entry->SetTimeout(context_.pipeTimeo);

    // --- REFACTORED PAYLOAD PREP ---
    std::vector<uint8_t> strippedData;
    pqact::BufferView view = FileOpsUtil::PreparePayload(prod, entry->GetFlags(), strippedData);

    int status = entry->Write(prod, view.data, view.size);

    if (status == EPIPE && !isNew) {
        context_.fileCache->RemoveAndFree(entry);
        entry = context_.fileCache->GetOrCreate<PipeEntry>(args, isNew);
        if (entry) {
            entry->SetTimeout(context_.pipeTimeo);
            status = entry->Write(prod, view.data, view.size);
            if (status){
              LogError("Couldn't re-pipe product to decoder \"{}\"", entry->GetPath());
            }
        } else {
            status = -1;
        }
    }

    if (status == 0 && entry->IsFlagSet(FL_FLUSH)) {
        status = entry->Sync(true);
        if (status){
          LogError("Couldn't flush pipe to decoder \"{}\"", entry->GetPath());
        }
    }

    if (status != 0 || entry->IsFlagSet(FL_CLOSE)) {
        context_.fileCache->RemoveAndFree(entry);
    }

    return status ? -1 : 0;
}

} // namespace pqact
} 
