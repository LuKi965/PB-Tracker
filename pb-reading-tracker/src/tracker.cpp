#include "tracker.h"
#include "metadata.h"
#include "db.h"
#include <inkview.h>
#include <string>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

extern bool g_daemon_mode;

// This is only used when tracker_init() is run inside InkView UI mode.
// The daemon loop in main.cpp polls at a lower frequency to reduce wakeups.
static const int POLL_MS = 15000;

static const int MIN_SESSION_SECONDS = 5;
// Save at most once per minute while the same book remains open. The older
// 15-second sync produced unnecessary SQLite writes and noisy logs.
static const int SYNC_INTERVAL_SECONDS = 60;

static std::string read_first_line(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return "";
    char buf[1024] = {0};
    char *r = fgets(buf, sizeof(buf) - 1, f);
    fclose(f);
    if (!r) return "";
    std::string s(buf);
    while (!s.empty() && (s[s.length() - 1] == '\n' || s[s.length() - 1] == '\r' || s[s.length() - 1] == ' '))
        s.erase(s.length() - 1);
    return s;
}

static bool looks_like_book(const std::string &path) {
    if (path.empty()) return false;
    const char *exts[] = { ".epub", ".pdf", ".fb2", ".djvu", ".mobi", ".cbz", ".cbr",
                           ".txt", ".doc", ".docx", ".rtf", ".htm", ".html", ".azw",
                           ".azw3", ".prc", NULL };
    std::string lower = path;
    for (size_t i = 0; i < lower.size(); i++)
        if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] += 32;
    for (int i = 0; exts[i]; i++) {
        size_t elen = strlen(exts[i]);
        if (lower.size() >= elen && lower.compare(lower.size() - elen, elen, exts[i]) == 0)
            return true;
    }
    return false;
}

static std::string try_currentbook() {
    return read_first_line(CURRENTBOOK);
}

static std::string try_lastopen() {
    return read_first_line(FLASHDIR "/system/state/lastopen.txt");
}

static std::string try_proc_scan() {
    DIR *proc = opendir("/proc");
    if (!proc) return "";

    int my_pid = getpid();
    char my_pid_str[16];
    snprintf(my_pid_str, sizeof(my_pid_str), "%d", my_pid);

    struct dirent *ent;
    while ((ent = readdir(proc)) != NULL) {
        bool is_pid = true;
        for (int i = 0; ent->d_name[i]; i++) {
            if (ent->d_name[i] < '0' || ent->d_name[i] > '9') { is_pid = false; break; }
        }
        if (!is_pid) continue;
        if (strcmp(ent->d_name, my_pid_str) == 0) continue;

        char fddir[128];
        snprintf(fddir, sizeof(fddir), "/proc/%s/fd", ent->d_name);
        DIR *fds = opendir(fddir);
        if (!fds) continue;

        struct dirent *fdent;
        while ((fdent = readdir(fds)) != NULL) {
            char linkpath[256], target[1024];
            snprintf(linkpath, sizeof(linkpath), "%s/%s", fddir, fdent->d_name);
            ssize_t len = readlink(linkpath, target, sizeof(target) - 1);
            if (len <= 0) continue;
            target[len] = '\0';
            std::string t(target);
            if (t.compare(0, 8, "/mnt/ext") == 0 && looks_like_book(t)) {
                closedir(fds);
                closedir(proc);
                return t;
            }
        }
        closedir(fds);
    }
    closedir(proc);
    return "";
}

static time_t g_lastopen_mtime = 0;
static std::string g_lastopen_value;
static int g_no_proc_count = 0;
static int g_proc_scan_backoff = 0;

static std::string current_book_path() {
    std::string path = try_currentbook();
    if (!path.empty() && looks_like_book(path)) {
        g_no_proc_count = 0;
        g_proc_scan_backoff = 0;
        return path;
    }

    const char *lastopen_path = FLASHDIR "/system/state/lastopen.txt";
    struct stat st;
    if (stat(lastopen_path, &st) == 0) {
        if (st.st_mtime != g_lastopen_mtime) {
            g_lastopen_mtime = st.st_mtime;
            g_lastopen_value = try_lastopen();
            g_no_proc_count = 0;
            g_proc_scan_backoff = 0;
            if (looks_like_book(g_lastopen_value)) return g_lastopen_value;
        }
    }

    // /proc scan is useful but relatively expensive. Do it only every few
    // polls while we do not already have a last-opened candidate.
    if (g_lastopen_value.empty()) {
        if (g_proc_scan_backoff > 0) {
            g_proc_scan_backoff--;
        } else {
            path = try_proc_scan();
            g_proc_scan_backoff = 2;
            if (!path.empty()) {
                g_no_proc_count = 0;
                return path;
            }
        }
    }

    if (!g_lastopen_value.empty()) {
        g_no_proc_count++;
        if (g_no_proc_count < 4) return g_lastopen_value;
        db_log("current_book_path: no active reader detected; clearing last book '%s'.", g_lastopen_value.c_str());
        g_lastopen_value.clear();
    }

    return "";
}

struct ActiveSession {
    bool active;
    std::string path;
    BookMeta meta;
    time_t start_time;
    time_t last_sync_time;
    ActiveSession() : active(false), start_time(0), last_sync_time(0) {}
};

static ActiveSession g_session;

static void close_session(time_t end_time) {
    if (!g_session.active) return;
    long total_duration = (long)(end_time - g_session.start_time);
    long inc_duration = (long)(end_time - g_session.last_sync_time);

    if (total_duration >= MIN_SESSION_SECONDS) {
        bool ok = db_update_active_session(g_session.meta, g_session.start_time, end_time, inc_duration);
        db_log("close_session: %s '%s' total=%ld inc=%ld", ok ? "saved" : "FAILED", g_session.path.c_str(), total_duration, inc_duration);
    } else {
        db_log("close_session: ignored short session '%s' duration=%ld", g_session.path.c_str(), total_duration);
    }
    g_session = ActiveSession();
}

static void open_session(const std::string &path, time_t start_time) {
    g_session.active = true;
    g_session.path = path;
    g_session.meta = g_daemon_mode ? metadata_read_basic(path) : metadata_read(path);
    g_session.start_time = start_time;
    g_session.last_sync_time = start_time;
    db_log("open_session: '%s' title='%s'", path.c_str(), g_session.meta.title.c_str());
}

void tracker_poll() {
    std::string path = current_book_path();
    time_t now = time(NULL);

    if (path.empty()) {
        close_session(now);
        return;
    }

    if (!g_session.active) {
        open_session(path, now);
        return;
    }

    if (g_session.path != path) {
        close_session(now);
        open_session(path, now);
        return;
    }

    long inc_duration = (long)(now - g_session.last_sync_time);
    long total_duration = (long)(now - g_session.start_time);
    if (total_duration >= MIN_SESSION_SECONDS && inc_duration >= SYNC_INTERVAL_SECONDS) {
        bool ok = db_update_active_session(g_session.meta, g_session.start_time, now, inc_duration);
        if (!ok) db_log("tracker_poll: periodic sync FAILED for '%s' inc=%ld", g_session.path.c_str(), inc_duration);
        g_session.last_sync_time = now;
    }
}

void tracker_flush() {
    close_session(time(NULL));
}

void tracker_init() {
    SetHardTimer("pbrs_poll", tracker_poll, POLL_MS);
    tracker_poll();
}
