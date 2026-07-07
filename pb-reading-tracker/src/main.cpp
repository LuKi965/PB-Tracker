// pbreadstats: PocketBook Reading Stats.
//
// Product goals for this version:
// - Kobo-style reading statistics on PocketBook.
// - Explicit UI pages for overview, today, week, calendar, books,
//   months and years.
// - No invisible left/right touch zones. PocketBook users can configure
//   their own global tap zones, so this app reacts to page-turn actions
//   and physical keys instead of stealing arbitrary screen areas.

#include <inkview.h>
#include "tracker.h"
#include "db.h"
#include "metadata.h"
#include "i18n.h"

#include <string>
#include <vector>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <unistd.h>
#include <signal.h>
#include <cstdlib>
#include <cstring>

static const char *DB_PATH = FLASHDIR "/system/pbreadstats/reading_stats.db";

bool g_daemon_mode = false;

static ifont *g_font_title = NULL;
static ifont *g_font_body = NULL;
static ifont *g_font_small = NULL;
static ifont *g_font_huge = NULL;

static double g_scale = 1.0;
static int S(int px) { return (int)(px * g_scale); }

enum PageKind {
    PAGE_OVERVIEW,
    PAGE_TODAY,
    PAGE_WEEK,
    PAGE_CALENDAR,
    PAGE_BOOKS,
    PAGE_MONTH,
    PAGE_YEAR
};

struct PageDesc {
    PageKind kind;
    int year;
    int month; // 1-12 for PAGE_MONTH, otherwise 0
    std::string label;
};

struct PeriodWindow {
    time_t start;
    time_t end;
};

static std::vector<PageDesc> g_pages;
static int g_page_index = 0;

static time_t day_start(time_t when) {
    struct tm t = *localtime(&when);
    t.tm_hour = 0;
    t.tm_min = 0;
    t.tm_sec = 0;
    return mktime(&t);
}

static time_t add_days(time_t when, int days) {
    struct tm t = *localtime(&when);
    t.tm_mday += days;
    return mktime(&t);
}

static time_t month_start(int year, int month) {
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    t.tm_hour = 0;
    t.tm_min = 0;
    t.tm_sec = 0;
    return mktime(&t);
}

static time_t year_start(int year) {
    return month_start(year, 1);
}

static PeriodWindow today_window() {
    time_t now = time(NULL);
    time_t start = day_start(now);
    PeriodWindow w;
    w.start = start;
    w.end = add_days(start, 1);
    return w;
}

static PeriodWindow week_window() {
    time_t now = time(NULL);
    time_t start = day_start(now);
    struct tm *lt = localtime(&start);
    int days_since_monday = (lt->tm_wday + 6) % 7; // tm_wday: 0=Sunday
    start = add_days(start, -days_since_monday);

    PeriodWindow w;
    w.start = start;
    w.end = add_days(start, 7);
    return w;
}

static PeriodWindow month_window(int year, int month) {
    PeriodWindow w;
    w.start = month_start(year, month);
    w.end = (month == 12) ? month_start(year + 1, 1) : month_start(year, month + 1);
    return w;
}

static PeriodWindow year_window(int year) {
    PeriodWindow w;
    w.start = year_start(year);
    w.end = year_start(year + 1);
    return w;
}

static std::string truncate_to_width(std::string text, int max_width) {
    while (text.length() > 3 && StringWidth(text.c_str()) > max_width) {
        text = text.substr(0, text.length() - 4) + "...";
    }
    return text;
}

static long total_seconds_for_books(const std::vector<BookPeriodEntry> &books) {
    long total = 0;
    for (const auto &b : books) total += b.total_seconds;
    return total;
}

static int total_sessions_for_books(const std::vector<BookPeriodEntry> &books) {
    int total = 0;
    for (const auto &b : books) total += b.session_count;
    return total;
}

static PageDesc make_page(PageKind kind, int year, int month, const std::string &label) {
    PageDesc p;
    p.kind = kind;
    p.year = year;
    p.month = month;
    p.label = label;
    return p;
}

static void rebuild_pages() {
    PageKind current_kind = PAGE_OVERVIEW;
    int current_year = 0;
    int current_month = 0;
    if (!g_pages.empty() && g_page_index < (int)g_pages.size()) {
        current_kind = g_pages[g_page_index].kind;
        current_year = g_pages[g_page_index].year;
        current_month = g_pages[g_page_index].month;
    }

    g_pages.clear();
    g_pages.push_back(make_page(PAGE_OVERVIEW, 0, 0, tr("Overview")));
    g_pages.push_back(make_page(PAGE_TODAY, 0, 0, tr("Today")));
    g_pages.push_back(make_page(PAGE_WEEK, 0, 0, tr("This Week")));
    g_pages.push_back(make_page(PAGE_CALENDAR, 0, 0, tr("Calendar")));
    g_pages.push_back(make_page(PAGE_BOOKS, 0, 0, tr("Books")));

    for (const auto &m : db_get_monthly_stats(12)) {
        g_pages.push_back(make_page(PAGE_MONTH, m.year, m.month, month_year_i18n(m.year, m.month)));
    }
    for (const auto &y : db_get_yearly_stats(6)) {
        g_pages.push_back(make_page(PAGE_YEAR, y.year, 0, y.label));
    }

    g_page_index = 0;
    for (size_t i = 0; i < g_pages.size(); i++) {
        if (g_pages[i].kind == current_kind &&
            g_pages[i].year == current_year &&
            g_pages[i].month == current_month) {
            g_page_index = (int)i;
            break;
        }
    }
}

static void draw_centered(const char *text, int y, ifont *font, int color) {
    SetFont(font, color);
    int w = StringWidth(text);
    DrawString((ScreenWidth() - w) / 2, y, text);
}

static void draw_header(const std::string &title) {
    int h = S(52);
    FillArea(0, 0, ScreenWidth(), h, WHITE);
    DrawLine(0, h - 1, ScreenWidth(), h - 1, LGRAY);
    SetFont(g_font_title, BLACK);
    std::string fit = truncate_to_width(title, ScreenWidth() - S(48));
    int tw = StringWidth(fit.c_str());
    DrawString((ScreenWidth() - tw) / 2, S(12), fit.c_str());
}

static void draw_footer_pageinfo() {
    int fh = S(28);
    int fy = ScreenHeight() - fh;
    DrawLine(S(30), fy, ScreenWidth() - S(30), fy, LGRAY);

    char line[64];
    snprintf(line, sizeof(line), "%d / %d", g_page_index + 1, (int)g_pages.size());
    draw_centered(line, fy + S(6), g_font_small, DGRAY);
}

static void draw_divider(int y) {
    DrawLine(S(36), y, ScreenWidth() - S(36), y, LGRAY);
}

static void draw_stat_card(int x, int y, int w, int h,
                           const std::string &title,
                           const std::string &value,
                           const std::string &subtitle) {
    DrawRect(x, y, w, h, LGRAY);

    SetFont(g_font_small, DGRAY);
    std::string t = truncate_to_width(title, w - S(16));
    int tw = StringWidth(t.c_str());
    DrawString(x + (w - tw) / 2, y + S(10), t.c_str());

    SetFont(g_font_title, BLACK);
    std::string v = truncate_to_width(value, w - S(16));
    int vw = StringWidth(v.c_str());
    DrawString(x + (w - vw) / 2, y + S(38), v.c_str());

    SetFont(g_font_small, DGRAY);
    std::string s = truncate_to_width(subtitle, w - S(16));
    int sw = StringWidth(s.c_str());
    DrawString(x + (w - sw) / 2, y + h - S(24), s.c_str());
}

static void draw_book_row(const BookPeriodEntry &b, int y, int cover_w, int cover_h, int max_y) {
    if (y + cover_h > max_y) return;

    int x = S(32);
    int text_x = x + cover_w + S(16);
    int max_tw = ScreenWidth() - text_x - S(32);

    ibitmap *cover = metadata_cover(b.path, cover_w, cover_h);
    if (cover) {
        DrawBitmap(x, y, cover);
    } else {
        FillArea(x, y, cover_w, cover_h, LGRAY);
        DrawRect(x, y, cover_w, cover_h, DGRAY);
    }

    SetFont(g_font_body, BLACK);
    std::string title = b.title.empty() ? tr("(untitled)") : b.title;
    title = truncate_to_width(title, max_tw);
    DrawString(text_x, y + S(4), title.c_str());

    SetFont(g_font_small, DGRAY);
    std::string author = truncate_to_width(b.author, max_tw);
    if (!author.empty()) DrawString(text_x, y + S(28), author.c_str());

    std::string status;
    if (b.finished) status = std::string(tr("Finished")) + " · " + sessions_short_i18n(b.session_count);
    else status = percent_read_i18n((int)(b.progress * 100)) + " · " + sessions_short_i18n(b.session_count);
    status = truncate_to_width(status, max_tw);
    DrawString(text_x, y + S(52), status.c_str());

    int bar_y = y + S(74);
    int bar_w = max_tw;
    int bar_h = S(6);
    float progress = b.progress;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    DrawRect(text_x, bar_y, bar_w, bar_h, DGRAY);
    FillArea(text_x + 1, bar_y + 1, (int)((bar_w - 2) * progress), bar_h - 2, BLACK);
}

static void draw_book_total_row(const BookTotal &b, int y, int cover_w, int cover_h, int max_y) {
    if (y + cover_h > max_y) return;

    BookPeriodEntry e;
    e.path = b.path;
    e.title = b.title;
    e.author = b.author;
    e.total_seconds = b.total_seconds;
    e.session_count = b.session_count;
    e.progress = b.progress;
    e.finished = b.finished;
    draw_book_row(e, y, cover_w, cover_h, max_y);
}

static void draw_period_summary_cards(int y,
                                      const std::string &title1,
                                      const std::string &value1,
                                      const std::string &subtitle1,
                                      const std::string &title2,
                                      const std::string &value2,
                                      const std::string &subtitle2) {
    int gap = S(16);
    int margin = S(24);
    int w = (ScreenWidth() - margin * 2 - gap) / 2;
    int h = S(88);
    draw_stat_card(margin, y, w, h, title1, value1, subtitle1);
    draw_stat_card(margin + w + gap, y, w, h, title2, value2, subtitle2);
}

static void draw_overview() {
    draw_header(tr("Reading Life"));

    OverallStats overall = db_get_overall_stats();
    StreakInfo streak = db_get_streaks();

    PeriodWindow today = today_window();
    PeriodWindow week = week_window();

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    int cur_year = lt->tm_year + 1900;
    int cur_month = lt->tm_mon + 1;
    PeriodWindow month = month_window(cur_year, cur_month);
    PeriodWindow year = year_window(cur_year);

    std::vector<BookPeriodEntry> today_books = db_get_books_in_period(today.start, today.end);
    std::vector<BookPeriodEntry> week_books = db_get_books_in_period(week.start, week.end);
    std::vector<BookPeriodEntry> month_books = db_get_books_in_period(month.start, month.end);
    std::vector<BookPeriodEntry> year_books = db_get_books_in_period(year.start, year.end);

    int y = S(74);
    int cx = ScreenWidth() / 2;

    long total_h = overall.total_seconds / 3600;
    char val[32];
    snprintf(val, sizeof(val), "%ld", total_h);
    draw_centered(val, y, g_font_huge, BLACK);
    y += S(66);
    draw_centered(tr("Hours"), y, g_font_title, BLACK);
    y += S(28);
    draw_centered(tr("Total Hours Read"), y, g_font_body, DGRAY);
    y += S(36);

    draw_divider(y);
    y += S(18);

    int col_w = ScreenWidth() / 3;
    int c1 = col_w / 2;
    int c2 = col_w + col_w / 2;
    int c3 = col_w * 2 + col_w / 2;
    char buf[32];

    SetFont(g_font_title, BLACK);
    snprintf(buf, sizeof(buf), "%d", overall.finished_books);
    DrawString(c1 - StringWidth(buf) / 2, y, buf);
    snprintf(buf, sizeof(buf), "%d", overall.total_sessions);
    DrawString(c2 - StringWidth(buf) / 2, y, buf);
    snprintf(buf, sizeof(buf), "%d", streak.current_streak);
    DrawString(c3 - StringWidth(buf) / 2, y, buf);

    int label_y = y + S(30);
    SetFont(g_font_small, DGRAY);
    DrawString(c1 - StringWidth(tr("Books Finished")) / 2, label_y, tr("Books Finished"));
    DrawString(c2 - StringWidth(tr("Sessions")) / 2, label_y, tr("Sessions"));
    DrawString(c3 - StringWidth(tr("Day Streak")) / 2, label_y, tr("Day Streak"));
    y = label_y + S(34);

    draw_period_summary_cards(
        y,
        tr("Today"),
        format_duration_i18n(total_seconds_for_books(today_books)),
        books_count_i18n((int)today_books.size()),
        tr("This Week"),
        format_duration_i18n(total_seconds_for_books(week_books)),
        books_count_i18n((int)week_books.size())
    );
    y += S(100);

    draw_period_summary_cards(
        y,
        tr("This Month"),
        format_duration_i18n(total_seconds_for_books(month_books)),
        books_count_i18n((int)month_books.size()),
        tr("This Year"),
        format_duration_i18n(total_seconds_for_books(year_books)),
        books_count_i18n((int)year_books.size())
    );
    y += S(102);

    SetFont(g_font_title, BLACK);
    DrawString(S(24), y, tr("Recent Books"));
    y += S(34);

    std::vector<BookTotal> recent = db_get_book_totals(3);
    if (recent.empty()) {
        draw_centered(tr("No books recorded yet. Start reading!"), y + S(8), g_font_small, DGRAY);
    } else {
        int cover_w = S(46);
        int cover_h = S(64);
        int max_y = ScreenHeight() - S(34);
        for (size_t i = 0; i < recent.size(); i++) {
            if (y + cover_h > max_y) break;
            draw_book_total_row(recent[i], y, cover_w, cover_h, max_y);
            y += cover_h + S(14);
        }
    }

    (void)cx;
}

static void draw_period_detail(const std::string &header, time_t start, time_t end) {
    draw_header(header);

    std::vector<BookPeriodEntry> books = db_get_books_in_period(start, end);
    long total_seconds = total_seconds_for_books(books);
    int total_sessions = total_sessions_for_books(books);

    int y = S(76);
    draw_centered(format_duration_i18n(total_seconds).c_str(), y, g_font_huge, BLACK);
    y += S(66);
    draw_centered(tr("Total Hours"), y, g_font_title, BLACK);
    y += S(28);

    std::string detail = books_read_count_i18n((int)books.size()) + " · " + sessions_count_i18n(total_sessions);
    draw_centered(detail.c_str(), y, g_font_body, DGRAY);
    y += S(34);

    draw_divider(y);
    y += S(20);

    if (books.empty()) {
        draw_centered(tr("No books logged in this period."), y + S(12), g_font_body, DGRAY);
        return;
    }

    int cover_w = S(54);
    int cover_h = S(76);
    int max_y = ScreenHeight() - S(40);
    for (size_t i = 0; i < books.size(); i++) {
        if (y + cover_h > max_y) {
            char more[64];
            snprintf(more, sizeof(more), "+ %d", (int)(books.size() - i));
            draw_centered(more, y + S(10), g_font_body, DGRAY);
            break;
        }
        draw_book_row(books[i], y, cover_w, cover_h, max_y);
        y += cover_h + S(18);
        if (i < books.size() - 1) draw_divider(y - S(9));
    }
}

static void draw_calendar() {
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    int cur_year = lt->tm_year + 1900;
    int cur_month = lt->tm_mon + 1;
    int today_day = lt->tm_mday;

    draw_header(month_year_i18n(cur_year, cur_month));

    StreakInfo streak = db_get_streaks();
    int cx = ScreenWidth() / 2;
    int y = S(62);

    char streak_val[16];
    snprintf(streak_val, sizeof(streak_val), "%d", streak.current_streak);
    draw_centered(streak_val, y, g_font_huge, BLACK);
    y += S(66);
    draw_centered(tr("Day Streak"), y, g_font_title, BLACK);
    y += S(30);

    draw_divider(y);
    y += S(14);

    PeriodWindow m = month_window(cur_year, cur_month);
    std::vector<DayStat> days = db_get_daily_totals_in_period(m.start, m.end);

    long seconds_by_day[32] = {0};
    for (const auto &d : days) {
        if (d.day >= 1 && d.day <= 31) seconds_by_day[d.day] = d.total_seconds;
    }

    struct tm first = {};
    first.tm_year = cur_year - 1900;
    first.tm_mon = cur_month - 1;
    first.tm_mday = 1;
    mktime(&first);
    int start_weekday = first.tm_wday;

    int days_in_month = 31;
    {
        struct tm probe = first;
        probe.tm_mday = 32;
        mktime(&probe);
        if (probe.tm_mon != first.tm_mon) days_in_month = 32 - probe.tm_mday;
    }

    int margin = S(20);
    int grid_w = ScreenWidth() - margin * 2;
    int cell_w = grid_w / 7;
    int cell_h = S(38);

    SetFont(g_font_small, DGRAY);
    for (int i = 0; i < 7; i++) {
        const char *wd = weekday_short_i18n(i);
        int ww = StringWidth(wd);
        DrawString(margin + i * cell_w + (cell_w - ww) / 2, y, wd);
    }

    int y0 = y + S(22);
    for (int day = 1; day <= days_in_month; day++) {
        int slot = start_weekday + (day - 1);
        int col = slot % 7;
        int row = slot / 7;
        int dx = margin + col * cell_w;
        int dy = y0 + row * cell_h;

        long secs = seconds_by_day[day];
        int fill = WHITE;
        int text_color = BLACK;
        if (secs > 0 && secs < 30 * 60)              { fill = LGRAY; text_color = BLACK; }
        else if (secs >= 30 * 60 && secs < 2 * 3600) { fill = DGRAY; text_color = WHITE; }
        else if (secs >= 2 * 3600)                   { fill = BLACK; text_color = WHITE; }

        if (fill != WHITE) FillArea(dx + 2, dy + 2, cell_w - 4, cell_h - 4, fill);
        DrawRect(dx + 2, dy + 2, cell_w - 4, cell_h - 4, DGRAY);
        if (day == today_day) {
            DrawRect(dx + 1, dy + 1, cell_w - 2, cell_h - 2, BLACK);
            DrawRect(dx + 3, dy + 3, cell_w - 6, cell_h - 6, BLACK);
        }

        char num[4];
        snprintf(num, sizeof(num), "%d", day);
        SetFont(g_font_small, text_color);
        int nw = StringWidth(num);
        DrawString(dx + (cell_w - nw) / 2, dy + S(10), num);
    }

    SetFont(g_font_small, DGRAY);
    const char *legend = tr("Darker = more time read");
    int lw = StringWidth(legend);
    DrawString(cx - lw / 2, ScreenHeight() - S(42), legend);
}

static void draw_books() {
    draw_header(tr("Books"));

    std::vector<BookTotal> books = db_get_book_totals(0);
    if (books.empty()) {
        draw_centered(tr("No books recorded yet. Start reading!"), S(90), g_font_body, DGRAY);
        return;
    }

    int y = S(72);
    int cover_w = S(54);
    int cover_h = S(76);
    int max_y = ScreenHeight() - S(40);

    for (size_t i = 0; i < books.size(); i++) {
        if (y + cover_h > max_y) {
            char more[64];
            snprintf(more, sizeof(more), "+ %d", (int)(books.size() - i));
            draw_centered(more, y + S(10), g_font_body, DGRAY);
            break;
        }
        draw_book_total_row(books[i], y, cover_w, cover_h, max_y);
        y += cover_h + S(18);
        if (i < books.size() - 1) draw_divider(y - S(9));
    }
}

static void draw_dashboard() {
    ClearScreen();
    if (g_pages.empty()) rebuild_pages();

    if (g_page_index < 0) g_page_index = 0;
    if (g_page_index >= (int)g_pages.size()) g_page_index = (int)g_pages.size() - 1;

    const PageDesc &p = g_pages[g_page_index];

    if (p.kind == PAGE_OVERVIEW) {
        draw_overview();
    } else if (p.kind == PAGE_TODAY) {
        PeriodWindow w = today_window();
        draw_period_detail(tr("Today"), w.start, w.end);
    } else if (p.kind == PAGE_WEEK) {
        PeriodWindow w = week_window();
        draw_period_detail(tr("This Week"), w.start, w.end);
    } else if (p.kind == PAGE_CALENDAR) {
        draw_calendar();
    } else if (p.kind == PAGE_BOOKS) {
        draw_books();
    } else if (p.kind == PAGE_MONTH) {
        PeriodWindow w = month_window(p.year, p.month);
        draw_period_detail(p.label, w.start, w.end);
    } else {
        PeriodWindow w = year_window(p.year);
        draw_period_detail(p.label, w.start, w.end);
    }

    draw_footer_pageinfo();
    FullUpdate();
}

static void go_prev_page() {
    if (g_page_index > 0) {
        g_page_index--;
        draw_dashboard();
    }
}

static void go_next_page() {
    if (g_page_index < (int)g_pages.size() - 1) {
        g_page_index++;
        draw_dashboard();
    }
}

static int get_daemon_pid() {
    FILE *f = fopen("/tmp/pbreadstats.pid", "r");
    if (!f) return 0;
    int pid = 0;
    if (fscanf(f, "%d", &pid) != 1) {
        fclose(f);
        return 0;
    }
    fclose(f);

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d", pid);
    FILE *proc = fopen(path, "r");
    if (proc) {
        fclose(proc);
        return pid;
    }
    return 0;
}

static std::string get_executable_path() {
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        return std::string(buf);
    }
    return "";
}

static void start_daemon() {
    if (get_daemon_pid() > 0) {
        db_log("start_daemon: Daemon is already running.");
        return;
    }
    std::string exe_path = get_executable_path();
    if (exe_path.empty()) {
        db_log("start_daemon: Failed to resolve executable path.");
        return;
    }
    std::string cmd = "\"" + exe_path + "\" --daemon &";
    db_log("start_daemon: Launching daemon command: %s", cmd.c_str());
    system(cmd.c_str());
}

static void sigterm_handler(int signum) {
    db_log("sigterm_handler: Received signal %d. Flushing tracker...", signum);
    tracker_flush();
    db_close();
    db_log("sigterm_handler: DB closed. Exiting daemon.");
    exit(0);
}

static void sigusr1_handler(int signum) {
    db_log("sigusr1_handler: Received signal %d. Forcing tracker flush...", signum);
    tracker_flush();
}

static void run_daemon() {
    g_daemon_mode = true;
    db_log("run_daemon: Daemon process started (PID: %d)", getpid());

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    signal(SIGUSR1, sigusr1_handler);

    FILE *f = fopen("/tmp/pbreadstats.pid", "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }

    if (!db_open(DB_PATH)) {
        db_log("run_daemon: Failed to open DB. Exiting daemon.");
        return;
    }

    db_log("run_daemon: DB opened successfully. Starting poll loop...");

    tracker_poll();

    while (true) {
        sleep(15);
        tracker_poll();
    }
}

static int main_handler(int type, int par1, int par2) {
    switch (type) {
        case EVT_INIT:
            db_log("EVT_INIT: Starting application initialization.");
            i18n_init();
            {
                g_scale = ScreenWidth() / 600.0;
                if (g_scale < 1.0) g_scale = 1.0;
                int size_title = (int)(26 * g_scale);
                int size_body  = (int)(20 * g_scale);
                int size_small = (int)(15 * g_scale);
                int size_huge  = (int)(64 * g_scale);
                g_font_title = OpenFont(DEFAULTFONTB, size_title, 1);
                g_font_body  = OpenFont(DEFAULTFONT, size_body, 1);
                g_font_small = OpenFont(DEFAULTFONT, size_small, 1);
                g_font_huge  = OpenFont(DEFAULTFONTB, size_huge, 1);
            }
            iv_buildpath(DB_PATH);
            if (!db_open(DB_PATH)) {
                db_log("EVT_INIT: db_open failed for path: %s", DB_PATH);
                Message(ICON_ERROR, tr("Database Error"), tr("Failed to open or initialize the database reading_stats.db."), 5000);
                CloseApp();
                return 1;
            }
            db_log("EVT_INIT: db_open succeeded. Checking daemon...");
            start_daemon();
            return 1;

        case EVT_SHOW:
            db_log("EVT_SHOW: Rendering dashboard.");
            {
                int pid = get_daemon_pid();
                if (pid > 0) {
                    db_log("EVT_SHOW: Sending SIGUSR1 to daemon (PID %d) to flush session.", pid);
                    kill(pid, SIGUSR1);
                    usleep(150000);
                }
            }
            rebuild_pages();
            draw_dashboard();
            return 1;

        case EVT_PREVPAGE:
            db_log("EVT_PREVPAGE: previous stats page.");
            go_prev_page();
            return 1;

        case EVT_NEXTPAGE:
            db_log("EVT_NEXTPAGE: next stats page.");
            go_next_page();
            return 1;

        case EVT_POINTERUP:
        case EVT_TOUCHUP:
            db_log("Touch event ignored by app UI: type=%d x=%d y=%d. PocketBook tap zones remain in control.", type, par1, par2);
            return 0;

        case EVT_KEYPRESS:
            db_log("EVT_KEYPRESS: key=%d", par1);
            if (par1 == KEY_BACK) {
                CloseApp();
            } else if (par1 == KEY_RIGHT || par1 == KEY_NEXT || par1 == KEY_NEXT2) {
                go_next_page();
            } else if (par1 == KEY_LEFT || par1 == KEY_PREV || par1 == KEY_PREV2) {
                go_prev_page();
            }
            return 1;

        case EVT_EXIT:
            db_log("EVT_EXIT: Closing GUI app.");
            db_close();
            if (g_font_title) CloseFont(g_font_title);
            if (g_font_body) CloseFont(g_font_body);
            if (g_font_small) CloseFont(g_font_small);
            if (g_font_huge) CloseFont(g_font_huge);
            return 1;

        default:
            return 0;
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--daemon") == 0) {
        run_daemon();
        return 0;
    }

    InkViewMain(main_handler);
    return 0;
}

extern "C" {
    char* secure_getenv(const char* name) {
        return getenv(name);
    }
    int getentropy(void *buffer, size_t length) {
        (void)buffer;
        (void)length;
        return -1;
    }
}
