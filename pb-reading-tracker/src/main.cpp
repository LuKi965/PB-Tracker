// pbreadstats: PocketBook Reading Stats.
// Product direction:
// - Kobo-like dashboard first, technical history screens second.
// - Input handling is implemented in the app source, not patched during build.
// - PocketBook nine-zone touch model is respected: only middle-left,
//   middle-center and middle-right are handled by the app.

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

#ifndef KEY_PREV
#define KEY_PREV KEY_LEFT
#endif
#ifndef KEY_PREV2
#define KEY_PREV2 KEY_PREV
#endif
#ifndef KEY_NEXT2
#define KEY_NEXT2 KEY_NEXT
#endif

// Confirmed on PocketBook InkPad Color 3 debug logs:
// type=25/key=24 => left physical page key
// type=25/key=25 => right physical page key
// type=26        => release event, ignored
static const int IPC3_KEY_DOWN = 25;
static const int IPC3_KEY_UP = 26;
static const int IPC3_KEY_PREV = 24;
static const int IPC3_KEY_NEXT = 25;

static const char *DB_PATH = FLASHDIR "/system/pbreadstats/reading_stats.db";
static const char *CFG_PATH = FLASHDIR "/system/pbreadstats/config.cfg";
static const char *PID_PATH = "/tmp/pbreadstats.pid";

bool g_daemon_mode = false;

static ifont *g_font_title = NULL;
static ifont *g_font_body = NULL;
static ifont *g_font_small = NULL;
static ifont *g_font_huge = NULL;
static double g_scale = 1.0;
static int g_page_index = 0;
static bool g_daemon_enabled = true;
static bool g_db_ready = false;

static int S(int px) { return (int)(px * g_scale); }

enum PageKind {
    PAGE_DASHBOARD = 0,
    PAGE_ACTIVITY = 1,
    PAGE_LIBRARY = 2,
    PAGE_COUNT = 3
};

struct PeriodWindow {
    time_t start;
    time_t end;
};

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
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    return mktime(&t);
}

static PeriodWindow today_window() {
    PeriodWindow w;
    w.start = day_start(time(NULL));
    w.end = add_days(w.start, 1);
    return w;
}

static PeriodWindow week_window() {
    time_t start = day_start(time(NULL));
    struct tm *lt = localtime(&start);
    int days_since_monday = (lt->tm_wday + 6) % 7;
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

static std::string truncate_to_width(std::string text, int max_width) {
    while (text.length() > 3 && StringWidth(text.c_str()) > max_width) {
        text = text.substr(0, text.length() - 4) + "...";
    }
    return text;
}

static long total_seconds_for_books(const std::vector<BookPeriodEntry> &books) {
    long total = 0;
    for (size_t i = 0; i < books.size(); i++) total += books[i].total_seconds;
    return total;
}

static int total_sessions_for_books(const std::vector<BookPeriodEntry> &books) {
    int total = 0;
    for (size_t i = 0; i < books.size(); i++) total += books[i].session_count;
    return total;
}

static std::string int_text(long v) {
    char b[32];
    snprintf(b, sizeof(b), "%ld", v);
    return b;
}

static std::string avg_session_text(long total_seconds, int sessions) {
    if (sessions <= 0 || total_seconds <= 0) return "0";
    long avg_seconds = total_seconds / sessions;
    if (avg_seconds > 0 && avg_seconds < 60) return "<1";
    long minutes = (avg_seconds + 30) / 60;
    char b[32];
    snprintf(b, sizeof(b), "%ld", minutes);
    return b;
}

static int progress_percent(float progress) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    return (int)(progress * 100.0f + 0.5f);
}

static std::string day_label_i18n(const DayStat &d) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = d.year - 1900;
    t.tm_mon = d.month - 1;
    t.tm_mday = d.day;
    mktime(&t);
    char b[32];
    snprintf(b, sizeof(b), "%s %d", weekday_short_i18n(t.tm_wday), d.day);
    return b;
}

static bool book_has_native_progress(const BookTotal &book) {
    return book.native_last_seen > 0 || book.native_npage > 0 || book.imported_native || book.progress > 0.0f || book.finished;
}

static bool book_in_progress(const BookTotal &book) {
    return book_has_native_progress(book) && book.progress > 0.0f && !book.finished;
}

static int count_in_progress(const std::vector<BookTotal> &books) {
    int count = 0;
    for (size_t i = 0; i < books.size(); i++) if (book_in_progress(books[i])) count++;
    return count;
}

static int count_tracked_books(const std::vector<BookTotal> &books) {
    int count = 0;
    for (size_t i = 0; i < books.size(); i++) if (books[i].session_count > 0 || books[i].total_seconds > 0) count++;
    return count;
}

static bool cfg_value_disables(const char *line, const char *key) {
    size_t len = strlen(key);
    if (strncmp(line, key, len) != 0) return false;
    const char *p = line + len;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\'' || *p == '"') p++;
    return strncmp(p, "0", 1) == 0 || strncmp(p, "off", 3) == 0 || strncmp(p, "false", 5) == 0 || strncmp(p, "no", 2) == 0;
}

static bool daemon_enabled_from_config() {
    FILE *f = fopen(CFG_PATH, "r");
    if (!f) return true;
    char line[256];
    bool enabled = true;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (cfg_value_disables(p, "daemon")) {
            enabled = false;
            break;
        }
    }
    fclose(f);
    return enabled;
}

static bool ensure_db_open() {
    if (g_db_ready) return true;
    iv_buildpath(DB_PATH);
    g_db_ready = db_open(DB_PATH);
    return g_db_ready;
}

static void draw_text(int x, int y, ifont *font, int color, const char *text) {
    SetFont(font, color);
    DrawString(x, y, text);
}

static void draw_centered(const char *text, int y, ifont *font, int color) {
    SetFont(font, color);
    int w = StringWidth(text);
    DrawString((ScreenWidth() - w) / 2, y, text);
}

static void draw_centered_in(int x, int y, int w, ifont *font, int color, const char *text) {
    SetFont(font, color);
    int tw = StringWidth(text);
    DrawString(x + (w - tw) / 2, y, text);
}

static void draw_right(const char *text, int right, int y, ifont *font, int color) {
    SetFont(font, color);
    DrawString(right - StringWidth(text), y, text);
}

static void draw_section_label(const char *text, int y) {
    int x = S(26);
    int h = S(24);
    FillArea(x, y, ScreenWidth() - x * 2, h, LGRAY);
    draw_text(x + S(8), y + S(5), g_font_small, BLACK, text);
}

static void draw_metric(int x, int y, int w, const char *value, const char *label) {
    DrawLine(x, y, x, y + S(76), LGRAY);
    std::string v = truncate_to_width(value, w - S(18));
    std::string l = truncate_to_width(label, w - S(18));
    draw_centered_in(x, y + S(5), w, g_font_title, BLACK, v.c_str());
    draw_centered_in(x, y + S(42), w, g_font_small, DGRAY, l.c_str());
}

static void draw_metric_cell(int index, int count, int y, const std::string &value, const std::string &label) {
    int margin = S(28);
    int w = (ScreenWidth() - margin * 2) / count;
    int x = margin + index * w;
    draw_metric(x, y, w, value.c_str(), label.c_str());
}

static void draw_progress_bar(int x, int y, int w, int h, float progress) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    DrawRect(x, y, w, h, LGRAY);
    FillArea(x + 1, y + 1, (int)((w - 2) * progress), h - 2, BLACK);
}

static void draw_header() {
    FillArea(0, 0, ScreenWidth(), S(48), WHITE);
    draw_centered(tr("Reading Stats"), S(13), g_font_small, DGRAY);
    DrawLine(S(22), S(48), ScreenWidth() - S(22), S(48), LGRAY);
}

static void draw_footer() {
    const char *name = g_page_index == PAGE_DASHBOARD ? tr("Overview") : (g_page_index == PAGE_ACTIVITY ? tr("Activity") : tr("Library"));
    char b[96];
    snprintf(b, sizeof(b), "%s  ·  %d/%d", name, g_page_index + 1, PAGE_COUNT);
    DrawLine(S(36), ScreenHeight() - S(34), ScreenWidth() - S(36), ScreenHeight() - S(34), LGRAY);
    draw_centered(b, ScreenHeight() - S(27), g_font_small, DGRAY);
}

static void draw_current_book(const BookTotal *book, int y) {
    draw_section_label(tr("Currently Reading"), y);
    y += S(34);

    int cover_w = S(96);
    int cover_h = S(136);
    int x = S(34);
    int tx = x + cover_w + S(24);
    int max_tw = ScreenWidth() - tx - S(34);

    if (book) {
        ibitmap *cover = metadata_cover(book->path, cover_w, cover_h);
        if (cover) DrawBitmap(x, y, cover);
        else {
            FillArea(x, y, cover_w, cover_h, LGRAY);
            DrawRect(x, y, cover_w, cover_h, DGRAY);
            draw_centered_in(x, y + cover_h / 2 - S(10), cover_w, g_font_small, DGRAY, "PB");
        }

        std::string title = book->title.empty() ? tr("(untitled)") : book->title;
        std::string author = book->author;
        title = truncate_to_width(title, max_tw);
        author = truncate_to_width(author, max_tw);
        draw_text(tx, y + S(0), g_font_title, BLACK, title.c_str());
        if (!author.empty()) draw_text(tx, y + S(34), g_font_small, DGRAY, author.c_str());

        std::string progress = percent_read_i18n(progress_percent(book->progress));
        draw_text(tx, y + S(62), g_font_small, DGRAY, tr(book_has_native_progress(*book) ? "PocketBook progress" : "Book progress"));
        draw_text(tx, y + S(85), g_font_body, BLACK, progress.c_str());
        draw_progress_bar(tx, y + S(113), max_tw, S(10), book->progress);

        std::string tracked = std::string(tr("Tracked time")) + ": " + format_duration_i18n(book->total_seconds);
        tracked = truncate_to_width(tracked, max_tw);
        draw_text(tx, y + S(132), g_font_small, DGRAY, tracked.c_str());

        const char *note = (book->imported_native && book->total_seconds == 0) ? tr("Native progress, no tracked time yet") : tr("Time source: Reading Stats");
        std::string note_text = truncate_to_width(note, max_tw);
        draw_text(tx, y + S(153), g_font_small, DGRAY, note_text.c_str());
    } else {
        FillArea(x, y, cover_w, cover_h, LGRAY);
        DrawRect(x, y, cover_w, cover_h, DGRAY);
        draw_text(tx, y + S(20), g_font_body, DGRAY, tr("No reading data yet."));
        draw_text(tx, y + S(58), g_font_small, DGRAY, tr("Open a book and come back after a few minutes."));
    }
}

static void draw_dashboard_page() {
    draw_header();
    if (!ensure_db_open()) return;

    std::vector<BookTotal> recent = db_get_book_totals(1);
    BookTotal *book = recent.empty() ? NULL : &recent[0];
    OverallStats overall = db_get_overall_stats();
    StreakInfo streak = db_get_streaks();

    PeriodWindow today = today_window();
    PeriodWindow week = week_window();
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    PeriodWindow month = month_window(lt->tm_year + 1900, lt->tm_mon + 1);

    std::vector<BookPeriodEntry> today_books = db_get_books_in_period(today.start, today.end);
    std::vector<BookPeriodEntry> week_books = db_get_books_in_period(week.start, week.end);
    std::vector<BookPeriodEntry> month_books = db_get_books_in_period(month.start, month.end);

    int y = S(64);
    draw_current_book(book, y);
    y += S(232);

    DrawLine(S(30), y, ScreenWidth() - S(30), y, LGRAY);
    y += S(22);

    draw_metric_cell(0, 4, y, format_duration_i18n(total_seconds_for_books(today_books)), tr("Today"));
    draw_metric_cell(1, 4, y, format_duration_i18n(total_seconds_for_books(week_books)), tr("This Week"));
    draw_metric_cell(2, 4, y, avg_session_text(overall.total_seconds, overall.total_sessions), tr("Avg min/session"));
    draw_metric_cell(3, 4, y, int_text(streak.current_streak), tr("Day Streak"));
    y += S(98);

    draw_section_label(tr("Reading profile"), y);
    y += S(42);

    int finished = overall.finished_books;
    int total = overall.distinct_books;
    int pct = total > 0 ? (finished * 100 / total) : 0;
    char pctbuf[32];
    snprintf(pctbuf, sizeof(pctbuf), "%d%%", pct);

    int bx = S(34);
    int bw = ScreenWidth() - S(68);
    draw_text(bx, y, g_font_title, BLACK, pctbuf);
    draw_text(bx + S(88), y + S(6), g_font_small, DGRAY, tr("of known books are complete"));
    draw_progress_bar(bx, y + S(48), bw, S(12), total > 0 ? (float)finished / (float)total : 0.0f);
    y += S(86);

    draw_metric_cell(0, 3, y, int_text(finished), tr("Books Finished"));
    draw_metric_cell(1, 3, y, format_duration_i18n(overall.total_seconds), tr("Total read"));
    draw_metric_cell(2, 3, y, format_duration_i18n(total_seconds_for_books(month_books)), tr("This Month"));

    if (!g_daemon_enabled) {
        draw_centered(tr("Safe USB mode: background tracking is off"), ScreenHeight() - S(62), g_font_small, DGRAY);
    }
}

static void draw_activity_page() {
    draw_header();
    if (!ensure_db_open()) return;

    draw_section_label(tr("Recent activity"), S(64));
    std::vector<DayStat> days = db_get_daily_stats(14);
    int y = S(108);
    int margin = S(36);
    int row_h = S(48);
    int max_bar_w = ScreenWidth() - S(220);
    long max_seconds = 1;
    long total = 0;
    int active_days = 0;
    for (size_t i = 0; i < days.size(); i++) {
        if (days[i].total_seconds > max_seconds) max_seconds = days[i].total_seconds;
        if (days[i].total_seconds > 0) active_days++;
        total += days[i].total_seconds;
    }

    if (days.empty()) {
        draw_centered(tr("No reading data yet."), y + S(30), g_font_body, DGRAY);
        return;
    }

    draw_metric_cell(0, 3, y, format_duration_i18n(total), tr("Last 14 days"));
    draw_metric_cell(1, 3, y, int_text(active_days), tr("Reading days"));
    draw_metric_cell(2, 3, y, format_duration_i18n(max_seconds), tr("Best day"));
    y += S(108);

    int limit = std::min((int)days.size(), 9);
    for (int i = 0; i < limit; i++) {
        DayStat d = days[i];
        std::string label = truncate_to_width(day_label_i18n(d), S(110));
        draw_text(margin, y, g_font_small, DGRAY, label.c_str());
        int bw = (int)((double)max_bar_w * (double)d.total_seconds / (double)max_seconds);
        if (d.total_seconds > 0 && bw < S(6)) bw = S(6);
        FillArea(margin + S(120), y + S(5), bw, S(16), BLACK);
        draw_right(format_duration_i18n(d.total_seconds).c_str(), ScreenWidth() - margin, y, g_font_small, BLACK);
        y += row_h;
    }
}

static void draw_book_row(const BookTotal &book, int x, int y, int cover_w, int cover_h, int max_y) {
    if (y + cover_h > max_y) return;
    ibitmap *cover = metadata_cover(book.path, cover_w, cover_h);
    if (cover) DrawBitmap(x, y, cover);
    else {
        FillArea(x, y, cover_w, cover_h, LGRAY);
        DrawRect(x, y, cover_w, cover_h, DGRAY);
    }

    int tx = x + cover_w + S(18);
    int tw = ScreenWidth() - tx - S(32);
    std::string title = book.title.empty() ? tr("(untitled)") : book.title;
    title = truncate_to_width(title, tw);
    draw_text(tx, y + S(0), g_font_body, BLACK, title.c_str());

    std::string progress = percent_read_i18n(progress_percent(book.progress));
    std::string sub = progress + " · " + format_duration_i18n(book.total_seconds);
    sub = truncate_to_width(sub, tw);
    draw_text(tx, y + S(24), g_font_small, DGRAY, sub.c_str());

    std::string source;
    if (book.imported_native && book.total_seconds == 0) source = tr("Started before Reading Stats");
    else if (book.session_count > 0) source = std::string(tr("Tracked")) + " · " + sessions_short_i18n(book.session_count);
    else source = tr("Progress source: PocketBook");
    source = truncate_to_width(source, tw);
    draw_text(tx, y + S(43), g_font_small, DGRAY, source.c_str());

    draw_progress_bar(tx, y + S(61), tw, S(7), book.progress);
}

static void draw_library_page() {
    draw_header();
    if (!ensure_db_open()) return;

    OverallStats overall = db_get_overall_stats();
    std::vector<BookTotal> books = db_get_book_totals(40);

    int in_progress_count = count_in_progress(books);
    int tracked_count = count_tracked_books(books);

    int y = S(64);
    draw_section_label(tr("Library"), y);
    y += S(42);

    draw_metric_cell(0, 3, y, int_text(in_progress_count), tr("In progress"));
    draw_metric_cell(1, 3, y, int_text(overall.finished_books), tr("Finished"));
    draw_metric_cell(2, 3, y, int_text(tracked_count), tr("Tracked books"));
    y += S(92);

    if (books.empty()) {
        draw_centered(tr("No reading data yet."), y + S(30), g_font_body, DGRAY);
        return;
    }

    int cover_w = S(38);
    int cover_h = S(54);
    int x = S(32);
    int max_y = ScreenHeight() - S(52);

    draw_section_label(tr("In progress"), y);
    y += S(34);
    int shown = 0;
    for (size_t i = 0; i < books.size() && shown < 3; i++) {
        if (!book_in_progress(books[i])) continue;
        if (y + cover_h > max_y) break;
        draw_book_row(books[i], x, y, cover_w, cover_h, max_y);
        y += cover_h + S(14);
        shown++;
    }
    if (shown == 0) {
        draw_text(x, y, g_font_small, DGRAY, tr("No books yet"));
        y += S(32);
    }

    if (y + S(112) < max_y) {
        draw_section_label(tr("Recently tracked"), y);
        y += S(34);
        shown = 0;
        for (size_t i = 0; i < books.size() && shown < 2; i++) {
            if (books[i].session_count <= 0 && books[i].total_seconds <= 0) continue;
            if (y + cover_h > max_y) break;
            draw_book_row(books[i], x, y, cover_w, cover_h, max_y);
            y += cover_h + S(14);
            shown++;
        }
        if (shown == 0) {
            draw_text(x, y, g_font_small, DGRAY, tr("No reading data yet."));
        }
    }
}

static void draw_page() {
    ClearScreen();
    if (g_page_index < 0) g_page_index = 0;
    if (g_page_index >= PAGE_COUNT) g_page_index = PAGE_COUNT - 1;

    if (g_page_index == PAGE_DASHBOARD) draw_dashboard_page();
    else if (g_page_index == PAGE_ACTIVITY) draw_activity_page();
    else draw_library_page();

    draw_footer();
    FullUpdate();
}

static bool pid_belongs_to_this_app(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return false;
    buf[n] = '\0';
    for (size_t i = 0; i < n; i++) if (buf[i] == '\0') buf[i] = ' ';
    return strstr(buf, "ReadingStats.app") || strstr(buf, "Reading Stats.app") || strstr(buf, "pbreadstats");
}

static int get_daemon_pid() {
    FILE *f = fopen(PID_PATH, "r");
    if (!f) return 0;
    int pid = 0;
    if (fscanf(f, "%d", &pid) != 1) { fclose(f); unlink(PID_PATH); return 0; }
    fclose(f);

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d", pid);
    FILE *proc = fopen(path, "r");
    if (proc) fclose(proc);
    if (!proc || !pid_belongs_to_this_app(pid)) {
        unlink(PID_PATH);
        return 0;
    }
    return pid;
}

static void refresh_from_daemon() {
    int pid = get_daemon_pid();
    if (pid > 0) {
        kill(pid, SIGUSR1);
        usleep(150000);
    }
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
    if (!g_daemon_enabled) {
        db_log("start_daemon: disabled by config.cfg.");
        return;
    }
    if (get_daemon_pid() > 0) {
        db_log("start_daemon: daemon already running.");
        return;
    }
    unlink(PID_PATH);
    std::string exe_path = get_executable_path();
    if (exe_path.empty()) {
        db_log("start_daemon: failed to resolve executable path.");
        return;
    }
    std::string cmd = "\"" + exe_path + "\" --daemon &";
    db_log("start_daemon: %s", cmd.c_str());
    system(cmd.c_str());
    for (int i = 0; i < 10; i++) {
        usleep(100000);
        if (get_daemon_pid() > 0) return;
    }
}

static void stop_daemon_for_usb() {
    int pid = get_daemon_pid();
    if (pid > 0) {
        db_log("stop_daemon_for_usb: SIGTERM to daemon PID %d", pid);
        kill(pid, SIGTERM);
        usleep(300000);
    }
}

static void sigterm_handler(int signum) {
    db_log("sigterm_handler: received signal %d. Flushing tracker.", signum);
    tracker_flush();
    db_close();
    unlink(PID_PATH);
    exit(0);
}

static void sigusr1_handler(int signum) {
    (void)signum;
    tracker_poll();
}

static void run_daemon() {
    g_daemon_mode = true;
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    signal(SIGUSR1, sigusr1_handler);

    FILE *f = fopen(PID_PATH, "w");
    if (f) { fprintf(f, "%d\n", getpid()); fclose(f); }

    if (!db_open(DB_PATH)) {
        db_log("run_daemon: failed to open DB.");
        unlink(PID_PATH);
        return;
    }

    db_log("run_daemon: started PID %d.", getpid());
    tracker_poll();
    while (true) {
        sleep(30);
        tracker_poll();
    }
}

static void go_prev_page() {
    if (g_page_index > 0) {
        g_page_index--;
        draw_page();
    }
}

static void go_next_page() {
    if (g_page_index < PAGE_COUNT - 1) {
        g_page_index++;
        draw_page();
    }
}

static bool is_key_down_event(int type) {
    if (type == IPC3_KEY_DOWN) return true;
    if (type == EVT_KEYPRESS) return true;
#ifdef EVT_KEYDOWN
    if (type == EVT_KEYDOWN) return true;
#endif
    return false;
}

static bool is_key_up_event(int type) {
    if (type == IPC3_KEY_UP) return true;
#ifdef EVT_KEYUP
    if (type == EVT_KEYUP) return true;
#endif
#ifdef EVT_KEYRELEASE
    if (type == EVT_KEYRELEASE) return true;
#endif
    return false;
}

static int handle_touch(int type, int x, int y) {
    (void)type;
    int col = (x * 3) / ScreenWidth();
    int row = (y * 3) / ScreenHeight();
    if (col < 0) col = 0; if (col > 2) col = 2;
    if (row < 0) row = 0; if (row > 2) row = 2;

    if (row == 1 && col == 0) { go_prev_page(); return 1; }
    if (row == 1 && col == 2) { go_next_page(); return 1; }
    if (row == 1 && col == 1) { refresh_from_daemon(); draw_page(); return 1; }
    return 0;
}

static int handle_key(int type, int key) {
    db_log("Key down: type=%d key=%d", type, key);
    if (key == KEY_BACK) {
        CloseApp();
        return 1;
    }
    if (key == IPC3_KEY_NEXT || key == KEY_RIGHT || key == KEY_NEXT || key == KEY_NEXT2) {
        go_next_page();
        return 1;
    }
    if (key == IPC3_KEY_PREV || key == KEY_LEFT || key == KEY_PREV || key == KEY_PREV2) {
        go_prev_page();
        return 1;
    }
    return 0;
}

static int main_handler(int type, int par1, int par2) {
    if (is_key_down_event(type)) return handle_key(type, par1);
    if (is_key_up_event(type)) return 1;

    switch (type) {
        case EVT_INIT:
            i18n_init();
            g_scale = ScreenWidth() / 600.0;
            if (g_scale < 1.0) g_scale = 1.0;
            g_font_title = OpenFont(DEFAULTFONTB, (int)(25 * g_scale), 1);
            g_font_body  = OpenFont(DEFAULTFONT,  (int)(19 * g_scale), 1);
            g_font_small = OpenFont(DEFAULTFONT,  (int)(14 * g_scale), 1);
            g_font_huge  = OpenFont(DEFAULTFONTB, (int)(52 * g_scale), 1);
            g_daemon_enabled = daemon_enabled_from_config();
            if (!ensure_db_open()) {
                Message(ICON_ERROR, tr("Database Error"), tr("Failed to open or initialize the database reading_stats.db."), 5000);
                CloseApp();
                return 1;
            }
            start_daemon();
            return 1;

        case EVT_SHOW:
            ensure_db_open();
            if (g_daemon_enabled) refresh_from_daemon();
            draw_page();
            return 1;

        case EVT_PREVPAGE:
            go_prev_page();
            return 1;

        case EVT_NEXTPAGE:
            go_next_page();
            return 1;

        case EVT_POINTERUP:
        case EVT_TOUCHUP:
            return handle_touch(type, par1, par2);

#ifdef EVT_PANEL_USBDRIVE
        case EVT_PANEL_USBDRIVE:
            db_log("EVT_PANEL_USBDRIVE: stopping daemon and closing database.");
            stop_daemon_for_usb();
            db_close();
            g_db_ready = false;
            CloseApp();
            return 0;
#endif

        case EVT_EXIT:
            db_log("EVT_EXIT: closing GUI app.");
            db_close();
            g_db_ready = false;
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
