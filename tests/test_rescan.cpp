#include "dali_rescan.h"
#include "framework.h"

using namespace dali_rescan;

// Drive a rescan to completion against a fake bus; returns the number of steps
// (bus queries + the final Finish) and checks each step's kind/address order.
static int run(Rescan& r, uint64_t bus, int* presence, int* groups) {
    int steps = 0;
    *presence = 0;
    *groups = 0;
    for (;;) {
        Step s = r.next();
        steps++;
        if (s.kind == StepKind::ProbePresence) {
            (*presence)++;
            r.report_presence(s.addr, (bus >> s.addr) & 1);
        } else if (s.kind == StepKind::QueryGroups) {
            (*groups)++;
            CHECK((bus >> s.addr) & 1);
            r.report_groups(s.addr);
        } else {
            CHECK(s.kind == StepKind::Finish);
            return steps;
        }
        if (steps > 1000) return -1;
    }
}

TEST(rescan_idle_by_default) {
    Rescan r;
    CHECK(!r.active());
    CHECK(r.next().kind == StepKind::None);
}

TEST(rescan_probes_every_address_then_groups_of_present_ones) {
    Rescan r;
    const uint64_t bus = (1ULL << 0) | (1ULL << 5) | (1ULL << 19) | (1ULL << 63);
    r.start(true);
    int presence, groups;
    int steps = run(r, bus, &presence, &groups);
    CHECK_EQ(presence, 64);
    CHECK_EQ(groups, 4);
    CHECK_EQ(steps, 64 + 4 + 1);
    CHECK(r.present_mask() == bus);
    CHECK(!r.active());
    CHECK(r.next().kind == StepKind::None);
}

TEST(rescan_without_groups_skips_group_queries) {
    Rescan r;
    r.start(false);
    int presence, groups;
    run(r, (1ULL << 3) | (1ULL << 4), &presence, &groups);
    CHECK_EQ(presence, 64);
    CHECK_EQ(groups, 0);
}

TEST(rescan_empty_bus_finishes) {
    Rescan r;
    r.start(true);
    int presence, groups;
    int steps = run(r, 0, &presence, &groups);
    CHECK_EQ(steps, 65);
    CHECK(r.present_mask() == 0);
}

TEST(rescan_same_step_until_reported) {
    Rescan r;
    r.start(true);
    Step a = r.next();
    Step b = r.next();
    CHECK(a.kind == StepKind::ProbePresence && a.addr == 0);
    CHECK(b.kind == StepKind::ProbePresence && b.addr == 0);
    r.report_presence(7, true);  // stale/out-of-order report is ignored
    CHECK(r.next().addr == 0);
    CHECK(r.present_mask() == 0);
}

TEST(rescan_restart_clears_previous_progress) {
    Rescan r;
    r.start(true);
    r.report_presence(0, true);
    r.start(true);
    CHECK(r.next().addr == 0);
    CHECK(r.present_mask() == 0);
}

TEST(rescan_cancel_goes_idle) {
    Rescan r;
    r.start(true);
    r.cancel();
    CHECK(!r.active());
    CHECK(r.next().kind == StepKind::None);
}
