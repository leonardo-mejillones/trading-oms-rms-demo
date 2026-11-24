#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace oms {

// Integer tick price. Floating-point construction is intentionally disallowed
// to avoid binary floating-point precision errors in trading calculations.
struct Price {
    int64_t ticks{0};

    constexpr Price() noexcept = default;

    template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    constexpr explicit Price(T t) noexcept : ticks(static_cast<int64_t>(t)) {}

    constexpr auto operator<=>(const Price&) const noexcept = default;

    constexpr Price operator+() const noexcept { return *this; }
    constexpr Price operator-() const noexcept { return Price(-ticks); }

    constexpr Price& operator+=(Price other) noexcept {
        ticks += other.ticks;
        return *this;
    }

    constexpr Price& operator-=(Price other) noexcept {
        ticks -= other.ticks;
        return *this;
    }

    friend constexpr Price operator+(Price lhs, Price rhs) noexcept {
        return Price(lhs.ticks + rhs.ticks);
    }

    friend constexpr Price operator-(Price lhs, Price rhs) noexcept {
        return Price(lhs.ticks - rhs.ticks);
    }

    friend constexpr Price operator*(Price p, int64_t multiplier) noexcept {
        return Price(p.ticks * multiplier);
    }

    friend constexpr Price operator*(int64_t multiplier, Price p) noexcept {
        return Price(p.ticks * multiplier);
    }

    friend constexpr Price operator/(Price p, int64_t divisor) {
        if (divisor == 0) {
            throw std::invalid_argument("Division by zero in Price");
        }
        return Price(p.ticks / divisor);
    }

    friend std::ostream& operator<<(std::ostream& os, Price p) {
        return os << p.ticks << " ticks";
    }
};

// Unsigned quantity in discrete units (e.g., shares/contracts).
// Floating-point construction is intentionally disallowed.
struct Quantity {
    uint64_t value{0};

    constexpr Quantity() noexcept = default;

    template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    constexpr explicit Quantity(T v) noexcept : value(static_cast<uint64_t>(v)) {}

    constexpr auto operator<=>(const Quantity&) const noexcept = default;

    constexpr Quantity& operator+=(Quantity other) noexcept {
        value += other.value;
        return *this;
    }

    constexpr Quantity& operator-=(Quantity other) {
        if (other.value > value) {
            throw std::invalid_argument("Quantity subtraction underflow");
        }
        value -= other.value;
        return *this;
    }

    friend constexpr Quantity operator+(Quantity lhs, Quantity rhs) noexcept {
        return Quantity(lhs.value + rhs.value);
    }

    friend constexpr Quantity operator-(Quantity lhs, Quantity rhs) {
        if (rhs.value > lhs.value) {
            throw std::invalid_argument("Quantity subtraction underflow");
        }
        return Quantity(lhs.value - rhs.value);
    }

    friend constexpr Quantity operator*(Quantity q, uint64_t multiplier) noexcept {
        return Quantity(q.value * multiplier);
    }

    friend constexpr Quantity operator*(uint64_t multiplier, Quantity q) noexcept {
        return Quantity(q.value * multiplier);
    }

    friend std::ostream& operator<<(std::ostream& os, Quantity q) {
        return os << q.value;
    }
};

struct OrderId {
    uint64_t value{0};

    constexpr OrderId() noexcept = default;
    constexpr explicit OrderId(uint64_t v) noexcept : value(v) {}

    constexpr auto operator<=>(const OrderId&) const noexcept = default;

    friend std::ostream& operator<<(std::ostream& os, OrderId id) {
        return os << "OrderId(" << id.value << ")";
    }
};

struct AccountId {
    uint64_t value{0};

    constexpr AccountId() noexcept = default;
    constexpr explicit AccountId(uint64_t v) noexcept : value(v) {}

    constexpr auto operator<=>(const AccountId&) const noexcept = default;

    friend std::ostream& operator<<(std::ostream& os, AccountId id) {
        return os << "AccountId(" << id.value << ")";
    }
};

enum class Side : uint8_t {
    Buy,
    Sell
};

constexpr std::string_view to_string(Side side) noexcept {
    switch (side) {
        case Side::Buy:  return "Buy";
        case Side::Sell: return "Sell";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, Side side) {
    return os << to_string(side);
}

enum class OrderType : uint8_t {
    Limit
};

constexpr std::string_view to_string(OrderType type) noexcept {
    switch (type) {
        case OrderType::Limit: return "Limit";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, OrderType type) {
    return os << to_string(type);
}

enum class TimeInForce : uint8_t {
    GTC, // Good 'Til Cancelled
    IOC  // Immediate Or Cancel
};

constexpr std::string_view to_string(TimeInForce tif) noexcept {
    switch (tif) {
        case TimeInForce::GTC: return "GTC";
        case TimeInForce::IOC: return "IOC";
    }
    return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, TimeInForce tif) {
    return os << to_string(tif);
}

} // namespace oms

namespace std {
template <>
struct hash<oms::OrderId> {
    std::size_t operator()(oms::OrderId id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};

template <>
struct hash<oms::AccountId> {
    std::size_t operator()(oms::AccountId id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};

template <>
struct hash<oms::Price> {
    std::size_t operator()(oms::Price p) const noexcept {
        return std::hash<int64_t>{}(p.ticks);
    }
};

template <>
struct hash<oms::Quantity> {
    std::size_t operator()(oms::Quantity q) const noexcept {
        return std::hash<uint64_t>{}(q.value);
    }
};
} // namespace std
