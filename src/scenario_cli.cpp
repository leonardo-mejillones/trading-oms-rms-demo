#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "oms/matching_engine.hpp"

namespace {

using namespace oms;

void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <scenario-file>\n";
}

void print_trades(const std::vector<Event>& events, std::size_t from) {
    for (std::size_t i = from; i < events.size(); ++i) {
        if (const auto* fill = std::get_if<Fill>(&events[i])) {
            std::cout << "TRADE order=" << fill->order_id.value
                      << " price=" << fill->price.ticks
                      << " qty=" << fill->quantity.value
                      << " account=" << fill->account.value
                      << " symbol=" << fill->symbol
                      << " side=" << to_string(fill->side)
                      << "\n";
        } else if (const auto* rej = std::get_if<OrderRejected>(&events[i])) {
            std::cout << "REJECTED order=" << rej->order_id.value
                      << " reason=" << rej->reason << "\n";
        } else if (const auto* canceled = std::get_if<OrderCanceled>(&events[i])) {
            std::cout << "CANCELED order=" << canceled->order_id.value << "\n";
        } else if (const auto* crej = std::get_if<CancelRejected>(&events[i])) {
            std::cout << "CANCEL_REJECTED order=" << crej->order_id.value
                      << " reason=" << crej->reason << "\n";
        }
    }
}

void print_book(const OrderBook& book, int64_t min_price = 90, int64_t max_price = 110) {
    std::cout << "BOOK_STATE\n";
    for (int64_t p = max_price; p >= min_price; --p) {
        const Quantity bid = book.depth_at(Side::Buy, Price{p});
        const Quantity ask = book.depth_at(Side::Sell, Price{p});
        if (bid.value != 0 || ask.value != 0) {
            std::cout << "  price=" << p
                      << " bid_qty=" << bid.value
                      << " ask_qty=" << ask.value
                      << "\n";
        }
    }
    if (const auto bb = book.best_bid()) {
        std::cout << "  best_bid=" << bb->ticks << "\n";
    }
    if (const auto ba = book.best_ask()) {
        std::cout << "  best_ask=" << ba->ticks << "\n";
    }
}

bool parse_side(const std::string& s, Side& out) {
    if (s == "BUY" || s == "Buy" || s == "buy") { out = Side::Buy; return true; }
    if (s == "SELL" || s == "Sell" || s == "sell") { out = Side::Sell; return true; }
    return false;
}

bool parse_type(const std::string& s, OrderType& out) {
    if (s == "LIMIT" || s == "Limit" || s == "limit") { out = OrderType::Limit; return true; }
    if (s == "MARKET" || s == "Market" || s == "market") { out = OrderType::Market; return true; }
    return false;
}

bool parse_tif(const std::string& s, TimeInForce& out) {
    if (s == "GTC" || s == "gtc") { out = TimeInForce::GTC; return true; }
    if (s == "IOC" || s == "ioc") { out = TimeInForce::IOC; return true; }
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return 2;
    }

    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "ERROR: cannot open scenario file: " << argv[1] << "\n";
        return 2;
    }

    std::unordered_map<std::string, InstrumentSpec> instruments;
    MatchingEngine engine;
    InMemoryEventSink sink;

    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd.empty() || cmd[0] == '#') {
            continue;
        }

        const std::size_t before = sink.events().size();

        if (cmd == "INSTRUMENT") {
            InstrumentSpec spec;
            if (!(iss >> spec.symbol >> spec.tick_size >> spec.lot_size)) {
                std::cerr << "ERROR: line " << line_no << ": invalid INSTRUMENT\n";
                return 1;
            }
            instruments[spec.symbol] = spec;
            engine = MatchingEngine(instruments); // reset engine with updated instruments
            // Note: resetting engine discards prior state; INSTRUMENT lines should be at top.
            sink.clear();
            std::cout << "INSTRUMENT symbol=" << spec.symbol
                      << " tick_size=" << spec.tick_size
                      << " lot_size=" << spec.lot_size << "\n";
            continue;
        }

        if (cmd == "SUBMIT") {
            uint64_t id = 0, account = 0, qty = 0, client_order_id = 0;
            int64_t price = 0;
            std::string side_s, type_s, tif_s, symbol;
            if (!(iss >> id >> account >> side_s >> price >> qty >> type_s >> tif_s >> client_order_id >> symbol)) {
                std::cerr << "ERROR: line " << line_no << ": invalid SUBMIT\n";
                return 1;
            }
            Side side{};
            OrderType type{};
            TimeInForce tif{};
            if (!parse_side(side_s, side) || !parse_type(type_s, type) || !parse_tif(tif_s, tif)) {
                std::cerr << "ERROR: line " << line_no << ": invalid SUBMIT enums\n";
                return 1;
            }

            OrderRequest req;
            req.id = OrderId{id};
            req.account = AccountId{account};
            req.client_order_id = ClientOrderId{client_order_id};
            req.symbol = symbol;
            req.side = side;
            req.price = Price{price};
            req.quantity = Quantity{qty};
            req.type = type;
            req.tif = tif;
            engine.process(SubmitCommand{req}, sink);
            print_trades(sink.events(), before);
            continue;
        }

        if (cmd == "CANCEL") {
            uint64_t id = 0;
            if (!(iss >> id)) {
                std::cerr << "ERROR: line " << line_no << ": invalid CANCEL\n";
                return 1;
            }
            engine.process(CancelCommand{OrderId{id}}, sink);
            print_trades(sink.events(), before);
            continue;
        }

        if (cmd == "REPLACE") {
            uint64_t id = 0, qty = 0;
            int64_t price = 0;
            if (!(iss >> id >> price >> qty)) {
                std::cerr << "ERROR: line " << line_no << ": invalid REPLACE\n";
                return 1;
            }
            engine.process(ReplaceCommand{OrderId{id}, Price{price}, Quantity{qty}}, sink);
            print_trades(sink.events(), before);
            continue;
        }

        std::cerr << "ERROR: line " << line_no << ": unknown command '" << cmd << "'\n";
        return 1;
    }

    print_book(engine.book());
    return 0;
}