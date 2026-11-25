#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "oms/oms.hpp"
#include "oms/validation.hpp"

namespace oms {

// Commands processed by the matching engine in submission order.
struct SubmitCommand {
    OrderRequest request;
};

struct CancelCommand {
    OrderId order_id{};
};

struct ReplaceCommand {
    OrderId order_id{};
    Price new_price{};
    Quantity new_quantity{};
};

using EngineCommand = std::variant<SubmitCommand, CancelCommand, ReplaceCommand>;

// Single-threaded matching engine facade.
// Processes commands in order: validation, risk, then matching.
// The same input sequence produces deterministic output.
class MatchingEngine {
public:
    explicit MatchingEngine(std::unordered_map<std::string, InstrumentSpec> instruments = {})
        : instruments_(std::move(instruments)) {}

    void process(const EngineCommand& command, EventSink& sink);

    void set_risk_pipeline(std::shared_ptr<RiskCheckPipeline> pipeline) {
        oms_.set_risk_pipeline(std::move(pipeline));
    }

    void set_position_tracker(std::shared_ptr<PositionTracker> tracker) {
        oms_.set_position_tracker(std::move(tracker));
    }

    void halt_account(AccountId account) { oms_.halt_account(account); }
    void resume_account(AccountId account) { oms_.resume_account(account); }
    [[nodiscard]] bool is_account_halted(AccountId account) const { return oms_.is_account_halted(account); }

    [[nodiscard]] Oms& oms() noexcept { return oms_; }
    [[nodiscard]] const Oms& oms() const noexcept { return oms_; }

    [[nodiscard]] OrderBook& book() noexcept { return oms_.book(); }
    [[nodiscard]] const OrderBook& book() const noexcept { return oms_.book(); }

    [[nodiscard]] const std::unordered_map<std::string, InstrumentSpec>& instruments() const noexcept {
        return instruments_;
    }

private:
    void process_submit(const SubmitCommand& cmd, EventSink& sink);
    void process_cancel(const CancelCommand& cmd, EventSink& sink);
    void process_replace(const ReplaceCommand& cmd, EventSink& sink);

    Oms oms_;
    std::unordered_map<std::string, InstrumentSpec> instruments_;
};

} // namespace oms