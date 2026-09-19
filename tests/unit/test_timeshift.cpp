// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T5 — time shift unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.7 (PP-FROZEN) / §4.5 and design §5.3.
//   * Delta mode: calendar months/years (day-of-month clamps), exact day/hour/minute/second.
//   * Timezone semantic mode: reinterpret the wall clock, OffsetTime* is written by build_plan
//     (covered in test_metadata.cpp).
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <cstdio>
#include <string>

#include "core/metadata.h"

namespace {

int g_failed = 0;

void check(bool ok, const std::string& case_name, const std::string& detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string show(const std::string& s) { return "'" + s + "'"; }

pp::TimeShift delta(int years = 0, int months = 0, int days = 0, int hours = 0, int minutes = 0,
                    int seconds = 0) {
    pp::TimeShift s;
    s.mode = pp::TimeShift::Mode::Delta;
    s.years = years;
    s.months = months;
    s.days = days;
    s.hours = hours;
    s.minutes = minutes;
    s.seconds = seconds;
    return s;
}

pp::TimeShift zone(int from_min, int to_min) {
    pp::TimeShift s;
    s.mode = pp::TimeShift::Mode::TimezoneSemantic;
    s.from_offset_min = from_min;
    s.to_offset_min = to_min;
    return s;
}

void expect_shift(const std::string& in, const pp::TimeShift& s, const std::string& want,
                  const std::string& case_name) {
    bool ok = false;
    const std::string got = pp::shift_exif_datetime(in, s, ok);
    check(ok && got == want, case_name,
          "in=" + show(in) + " want=" + show(want) + " got=" + show(got) +
              " ok=" + (ok ? "true" : "false"));
}

void expect_reject(const std::string& in, const pp::TimeShift& s, const std::string& case_name) {
    bool ok = true;
    const std::string got = pp::shift_exif_datetime(in, s, ok);
    check(!ok && got == in, case_name,
          "expected ok=false and the original string, got=" + show(got) +
              " ok=" + (ok ? "true" : "false"));
}

void expect_offset(int minutes, const std::string& want, const std::string& case_name) {
    const std::string got = pp::offset_time_string(minutes);
    check(got == want, case_name, "minutes=" + std::to_string(minutes) + " want=" + show(want) +
                                      " got=" + show(got));
}

}  // namespace

int main() {
    // ------------------------------------------------------------------ Delta (12+ cases)
    expect_shift("2024:03:15 10:00:00", delta(0, 1), "2024:04:15 10:00:00", "delta/plus-one-month");
    expect_shift("2024:01:31 10:00:00", delta(0, 1), "2024:02:29 10:00:00",
                 "delta/month-clamp-leap-year");
    expect_shift("2023:01:31 10:00:00", delta(0, 1), "2023:02:28 10:00:00",
                 "delta/month-clamp-non-leap");
    expect_shift("2024:12:15 00:00:00", delta(0, 1), "2025:01:15 00:00:00", "delta/cross-year-month");
    expect_shift("2024:02:29 12:00:00", delta(1), "2025:02:28 12:00:00", "delta/year-step-leap-day");
    expect_shift("2024:12:31 23:59:59", delta(0, 0, 0, 0, 0, 1), "2025:01:01 00:00:00",
                 "delta/cross-year-second");
    expect_shift("2024:03:01 00:30:00", delta(0, 0, 0, -1), "2024:02:29 23:30:00",
                 "delta/negative-hour-cross-month");
    expect_shift("2024:03:31 10:00:00", delta(0, -1), "2024:02:29 10:00:00",
                 "delta/negative-month-clamp");
    expect_shift("2024:01:31 23:30:00", delta(0, 1, 0, 0, 30), "2024:03:01 00:00:00",
                 "delta/month-then-minute-carry");
    expect_shift("2024:02:28 10:00:00", delta(0, 0, 1), "2024:02:29 10:00:00", "delta/plus-one-day-leap");
    expect_shift("2024:03:01 10:00:00", delta(0, 0, 0, 0, 90), "2024:03:01 11:30:00",
                 "delta/minutes-only");
    expect_shift("2024:03:01 10:00:00", delta(), "2024:03:01 10:00:00", "delta/zero-is-identity");
    expect_shift("2024:03:01 10:00:00", delta(0, 0, -400), "2023:01:26 10:00:00",
                 "delta/negative-days-cross-year");

    expect_reject("2024-03-01 10:00:00", delta(0, 1), "delta/reject-dash-format");
    expect_reject("2024:13:01 10:00:00", delta(0, 1), "delta/reject-month-13");
    expect_reject("2023:02:29 10:00:00", delta(0, 1), "delta/reject-non-leap-day");
    expect_reject("2024:03:01 24:00:00", delta(0, 1), "delta/reject-hour-24");
    expect_reject("", delta(0, 1), "delta/reject-empty");
    expect_reject("2024:03:01 10:00:00", delta(10000), "delta/reject-year-overflow");

    // ------------------------------------------------------- Timezone semantic (4+ cases)
    expect_shift("2024:03:01 10:00:00", zone(480, 540), "2024:03:01 11:00:00", "tz/plus-one-hour");
    expect_shift("2024:03:01 10:00:00", zone(540, 480), "2024:03:01 09:00:00", "tz/minus-one-hour");
    expect_shift("2024:03:01 00:30:00", zone(540, 480), "2024:02:29 23:30:00",
                 "tz/backward-cross-month");
    expect_shift("2024:03:01 10:00:00", zone(330, 480), "2024:03:01 12:30:00", "tz/half-hour-zones");
    expect_shift("2024:03:01 10:00:00", zone(480, 480), "2024:03:01 10:00:00", "tz/same-offset-noop");
    expect_reject("not a date", zone(480, 540), "tz/reject-invalid");

    // shift_exif_datetime() must route TimezoneSemantic through reinterpret_timezone().
    {
        bool ok = false;
        const std::string got = pp::shift_exif_datetime("2024:06:10 22:45:00", zone(-300, 480), ok);
        check(ok && got == "2024:06:11 11:45:00", "tz/routed-through-reinterpret",
              "got=" + show(got) + " ok=" + (ok ? "true" : "false"));
    }
    {
        bool ok = false;
        const std::string got = pp::reinterpret_timezone("2024:03:01 10:00:00", 480, 540, ok);
        check(ok && got == "2024:03:01 11:00:00", "tz/direct-call",
              "got=" + show(got) + " ok=" + (ok ? "true" : "false"));
    }

    // ------------------------------------------------------------- offset_time_string()
    expect_offset(480, "+08:00", "offset/+480");
    expect_offset(-330, "-05:30", "offset/-330");
    expect_offset(0, "+00:00", "offset/zero-is-plus");
    expect_offset(90, "+01:30", "offset/+90");
    expect_offset(-45, "-00:45", "offset/-45");
    expect_offset(1439, "+23:59", "offset/max");
    expect_offset(1500, "+23:59", "offset/clamped-high");
    expect_offset(-1500, "-23:59", "offset/clamped-low");

    // ------------------------------------------------------------------------- is_noop()
    check(pp::TimeShift{}.is_noop(), "noop/default-delta", "default TimeShift must be a no-op");
    check(delta().is_noop(), "noop/zero-delta", "all-zero delta must be a no-op");
    check(!delta(0, 0, 0, 0, 0, 1).is_noop(), "noop/one-second", "one second is a real shift");
    check(zone(480, 480).is_noop(), "noop/same-zone", "same offsets must be a no-op");
    check(!zone(480, 540).is_noop(), "noop/different-zone", "different offsets are a real shift");
    check(!delta(1).is_noop(), "noop/one-year", "one year is a real shift");

    check(pp::BatchRules{}.is_noop(), "noop/empty-batch-rules", "default BatchRules must be a no-op");
    {
        pp::BatchRules r;
        r.sync_mtime = true;
        check(!r.is_noop(), "noop/sync-mtime", "sync_mtime is a rule");
    }
    {
        pp::BatchRules r;
        r.gps_clear = true;
        check(!r.is_noop(), "noop/gps-clear", "gps_clear is a rule");
    }
    {
        pp::BatchRules r;
        r.strip_privacy = true;
        check(!r.is_noop(), "noop/strip-privacy", "strip_privacy is a rule");
    }
    {
        pp::BatchRules r;
        r.time_shift = delta();
        check(r.is_noop(), "noop/noop-time-shift", "a no-op time shift must not count");
    }
    {
        pp::BatchRules r;
        r.xmp_edits.push_back(pp::TagEdit{"Xmp.dc.title", std::string("x"), false});
        check(!r.is_noop(), "noop/one-edit", "a tag edit is a rule");
    }

    if (g_failed == 0) {
        std::printf("OK test_timeshift: all cases passed\n");
    } else {
        std::printf("FAILED test_timeshift: %d case(s)\n", g_failed);
    }
    return g_failed;
}
