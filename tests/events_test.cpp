#include <gtest/gtest.h>

#include <memory>
#include <variant>

#include "oms/event_sink.hpp"

using namespace oms;

TEST(EventsTest, OrderAcceptedEvent) {
    OrderAccepted event{OrderId{42}};
    EXPECT_EQ(event.order_id, OrderId{42});
}

TEST(EventsTest, OrderRejectedEventCarriesReason) {
    OrderRejected event{OrderId{7}, "insufficient buying power"};
    EXPECT_EQ(event.order_id, OrderId{7});
    EXPECT_EQ(event.reason, "insufficient buying power");
}

TEST(EventsTest, FillEvent) {
    Fill event{OrderId{1}, Price{10050}, Quantity{200}};
    EXPECT_EQ(event.order_id, OrderId{1});
    EXPECT_EQ(event.price, Price{10050});
    EXPECT_EQ(event.quantity, Quantity{200});
}

TEST(EventsTest, OrderCanceledEvent) {
    OrderCanceled event{OrderId{9}};
    EXPECT_EQ(event.order_id, OrderId{9});
}

TEST(EventsTest, CancelRejectedEventCarriesReason) {
    CancelRejected event{OrderId{9}, "order not found"};
    EXPECT_EQ(event.order_id, OrderId{9});
    EXPECT_EQ(event.reason, "order not found");
}

TEST(EventsTest, EventVariantHoldsAllTypes) {
    Event accepted = OrderAccepted{OrderId{1}};
    Event rejected = OrderRejected{OrderId{2}, "bad order"};
    Event fill = Fill{OrderId{3}, Price{100}, Quantity{10}};
    Event canceled = OrderCanceled{OrderId{4}};
    Event cancel_rejected = CancelRejected{OrderId{5}, "too late"};

    EXPECT_TRUE(std::holds_alternative<OrderAccepted>(accepted));
    EXPECT_TRUE(std::holds_alternative<OrderRejected>(rejected));
    EXPECT_TRUE(std::holds_alternative<Fill>(fill));
    EXPECT_TRUE(std::holds_alternative<OrderCanceled>(canceled));
    EXPECT_TRUE(std::holds_alternative<CancelRejected>(cancel_rejected));
}

TEST(EventSinkTest, InMemorySinkCapturesPublishedEvents) {
    InMemoryEventSink sink;
    EXPECT_TRUE(sink.empty());
    EXPECT_EQ(sink.size(), 0u);

    sink.publish(OrderAccepted{OrderId{1}});
    sink.publish(OrderRejected{OrderId{2}, "risk limit"});
    sink.publish(Fill{OrderId{1}, Price{500}, Quantity{10}});
    sink.publish(OrderCanceled{OrderId{1}});
    sink.publish(CancelRejected{OrderId{3}, "unknown order"});

    EXPECT_FALSE(sink.empty());
    EXPECT_EQ(sink.size(), 5u);

    ASSERT_TRUE(std::holds_alternative<OrderAccepted>(sink.events()[0]));
    ASSERT_TRUE(std::holds_alternative<OrderRejected>(sink.events()[1]));
    ASSERT_TRUE(std::holds_alternative<Fill>(sink.events()[2]));
    ASSERT_TRUE(std::holds_alternative<OrderCanceled>(sink.events()[3]));
    ASSERT_TRUE(std::holds_alternative<CancelRejected>(sink.events()[4]));

    const auto& rejected = std::get<OrderRejected>(sink.events()[1]);
    EXPECT_EQ(rejected.reason, "risk limit");
}

TEST(EventSinkTest, InMemorySinkFiltersByType) {
    InMemoryEventSink sink;
    sink.publish(OrderAccepted{OrderId{1}});
    sink.publish(Fill{OrderId{1}, Price{100}, Quantity{5}});
    sink.publish(Fill{OrderId{1}, Price{101}, Quantity{3}});
    sink.publish(OrderCanceled{OrderId{1}});

    const auto fills = sink.events_of_type<Fill>();
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].quantity, Quantity{5});
    EXPECT_EQ(fills[1].price, Price{101});

    const auto accepted = sink.events_of_type<OrderAccepted>();
    ASSERT_EQ(accepted.size(), 1u);
    EXPECT_EQ(accepted[0].order_id, OrderId{1});
}

TEST(EventSinkTest, CanUseThroughInterface) {
    std::unique_ptr<EventSink> sink = std::make_unique<InMemoryEventSink>();
    sink->publish(OrderAccepted{OrderId{10}});
    sink->publish(CancelRejected{OrderId{10}, "not active"});

    auto* memory_sink = dynamic_cast<InMemoryEventSink*>(sink.get());
    ASSERT_NE(memory_sink, nullptr);
    EXPECT_EQ(memory_sink->size(), 2u);
}

TEST(EventSinkTest, ClearRemovesAllEvents) {
    InMemoryEventSink sink;
    sink.publish(OrderAccepted{OrderId{1}});
    sink.publish(OrderCanceled{OrderId{1}});
    EXPECT_EQ(sink.size(), 2u);

    sink.clear();
    EXPECT_TRUE(sink.empty());
    EXPECT_EQ(sink.size(), 0u);
}
