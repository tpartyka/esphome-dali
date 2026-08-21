#include "framework.h"
#include "dali_transaction_queue.h"

namespace {

TEST(transaction_queue_coalesces_same_payload) {
    dali_transaction::Queue queue;
    int first = 0;
    queue.enqueue(&first, dali_transaction::Priority::BACKGROUND);
    queue.enqueue(&first, dali_transaction::Priority::INTERACTIVE);

    void *out = nullptr;
    CHECK_EQ(queue.size(), 1u);
    CHECK(queue.take_next(out));
    CHECK_EQ(out, static_cast<void *>(&first));
    CHECK(queue.empty());
}

TEST(transaction_queue_prioritizes_interactive_work) {
    dali_transaction::Queue queue;
    int background = 0;
    int interactive = 0;
    queue.enqueue(&background, dali_transaction::Priority::BACKGROUND);
    queue.enqueue(&interactive, dali_transaction::Priority::INTERACTIVE);

    void *out = nullptr;
    CHECK(queue.take_next(out));
    CHECK_EQ(out, static_cast<void *>(&interactive));
    CHECK(queue.take_next(out));
    CHECK_EQ(out, static_cast<void *>(&background));
}

}  // namespace
