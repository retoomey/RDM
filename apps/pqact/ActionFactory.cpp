#include "ActionFactory.h"
#include "ExecAction.h"
#include "FileAction.h" 
#include "PipeAction.h" 
#include "Product.h"
#include "Log.h"
#include <cctype>

namespace rdm {
namespace pqact {

class StubAction : public IAction {
private:
    std::string name_;
public:
    StubAction(PqactContext& ctx, std::string name) : IAction(ctx), name_(std::move(name)) {}
    
    int Execute(const Product& prod, const std::vector<std::string>& args, const void* xprod, size_t xlen) override {
        LogDebug("StubAction::Execute called for action [{}] on product [{}]", name_, prod.info.ident);
        // Return 0 (success) to keep the queue sequence moving during our stubbing phase.
        return 0; 
    }
    
    const char* GetName() const override { return name_.c_str(); }
};

std::unique_ptr<IAction> ActionFactory::Create(const std::string& actionName, PqactContext& ctx) {
    std::string lowerName = actionName;
    for (auto& c : lowerName) c = std::tolower(c);

    if (lowerName == "exec") {
        return std::make_unique<ExecAction>(ctx);
    } else if (lowerName == "file") {
        return std::make_unique<FileAction>(ctx); 
    } else if (lowerName == "pipe") {
        return std::make_unique<PipeAction>(ctx);
    }

    return std::make_unique<StubAction>(ctx, lowerName);
}

} // namespace pqact
} 
