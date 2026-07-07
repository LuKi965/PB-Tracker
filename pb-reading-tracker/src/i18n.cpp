#include "i18n.h"

#include <inkview.h>

#include <cstdio>
#include <cstring>
#include <cctype>

namespace {

enum Lang {
    LANG_EN,
    LANG_PL
};

Lang g_lang = LANG_EN;

struct Translation {
    const char *key;
    const char *pl;
};

const Translation TRANSLATIONS[] = {
    {"Overview", "Podsumowanie"},
    {"Calendar", "Kalendarz"},
    {"Reading Life", "Statystyki czytania"},
    {"Hours", "Godziny"},
    {"Total Hours Read", "Łączny czas czytania"},
    {"Books Finished", "Ukończone książki"},
    {"Sessions", "Sesje"},
    {"Day Streak", "Seria dni"},
    {"This Month", "Ten miesiąc"},
    {"This Year", "Ten rok"},
    {"Recent Books", "Ostatnie książki"},
    {"Finished", "Ukończono"},
    {"read", "przeczytano"},
    {"(untitled)", "(bez tytułu)"},
    {"Total Hours", "Łączny czas"},
    {"No books recorded yet. Start reading!", "Brak zapisanych książek. Zacznij czytać!"},
    {"No books logged in this period.", "Brak książek w tym okresie."},
    {"Darker = more time read", "Ciemniej = więcej czytania"},
    {"Database Error", "Błąd bazy danych"},
    {"Failed to open or initialize the database reading_stats.db.", "Nie udało się otworzyć lub zainicjalizować bazy reading_stats.db."},
    {NULL, NULL}
};

void set_language_from_code(const char *code) {
    if (!code || !*code) return;

    if (strncmp(code, "pl", 2) == 0 || strncmp(code, "PL", 2) == 0) {
        g_lang = LANG_PL;
        return;
    }

    if (strncmp(code, "en", 2) == 0 || strncmp(code, "EN", 2) == 0) {
        g_lang = LANG_EN;
        return;
    }
}

bool key_matches(const char *line, const char *key) {
    size_t len = strlen(key);
    return strncmp(line, key, len) == 0 &&
           (line[len] == ' ' || line[len] == '\t' || line[len] == '=');
}

bool read_language_from_file(const char *path, char *out, size_t out_size) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[256];

    while (fgets(line, sizeof(line), f)) {
        char *p = line;

        while (*p == ' ' || *p == '\t') p++;

        if (!key_matches(p, "language") && !key_matches(p, "lang")) {
            continue;
        }

        while (*p && *p != '=') p++;
        if (*p == '=') p++;

        while (*p == ' ' || *p == '\t' || *p == '"' || *p == '\'') p++;

        char *start = p;

        while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-')) {
            p++;
        }

        *p = '\0';

        if (*start) {
            strncpy(out, start, out_size - 1);
            out[out_size - 1] = '\0';
            fclose(f);
            return true;
        }
    }

    fclose(f);
    return false;
}

const char *book_form_pl(int count) {
    int n10 = count % 10;
    int n100 = count % 100;

    if (count == 1) return "książka";
    if (n10 >= 2 && n10 <= 4 && !(n100 >= 12 && n100 <= 14)) return "książki";
    return "książek";
}

} // namespace

void i18n_init() {
    g_lang = LANG_EN;

    char code[32];

#ifdef GLOBALCONFIGFILE
    if (read_language_from_file(GLOBALCONFIGFILE, code, sizeof(code))) {
        set_language_from_code(code);
    }
#endif

    if (read_language_from_file(FLASHDIR "/system/config/global.cfg", code, sizeof(code))) {
        set_language_from_code(code);
    }

    // Manual override. Create this file on the device if auto-detection fails:
    // /system/pbreadstats/config.cfg
    if (read_language_from_file(FLASHDIR "/system/pbreadstats/config.cfg", code, sizeof(code))) {
        set_language_from_code(code);
    }
}

bool i18n_is_pl() {
    return g_lang == LANG_PL;
}

const char *tr(const char *key) {
    if (!key) return "";

    if (g_lang != LANG_PL) {
        return key;
    }

    for (int i = 0; TRANSLATIONS[i].key; i++) {
        if (strcmp(TRANSLATIONS[i].key, key) == 0) {
            return TRANSLATIONS[i].pl;
        }
    }

    return key;
}

const char *month_name_i18n(int month) {
    static const char *EN[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    static const char *PL[] = {
        "sty", "lut", "mar", "kwi", "maj", "cze",
        "lip", "sie", "wrz", "paź", "lis", "gru"
    };

    if (month < 1 || month > 12) return "";
    return g_lang == LANG_PL ? PL[month - 1] : EN[month - 1];
}

const char *weekday_short_i18n(int weekday) {
    // Matches tm_wday: 0 = Sunday.
    static const char *EN[] = {"S", "M", "T", "W", "T", "F", "S"};
    static const char *PL[] = {"N", "P", "W", "Ś", "C", "P", "S"};

    if (weekday < 0 || weekday > 6) return "";
    return g_lang == LANG_PL ? PL[weekday] : EN[weekday];
}

std::string format_hms_i18n(long seconds) {
    long h = seconds / 3600;
    long m = (seconds % 3600) / 60;

    char buf[64];

    if (g_lang == LANG_PL) {
        snprintf(buf, sizeof(buf), "%ld godz. %02ld min", h, m);
    } else {
        snprintf(buf, sizeof(buf), "%ldh %02ldm", h, m);
    }

    return std::string(buf);
}

std::string books_count_i18n(int count) {
    char buf[64];

    if (g_lang == LANG_PL) {
        snprintf(buf, sizeof(buf), "%d %s", count, book_form_pl(count));
    } else {
        snprintf(buf, sizeof(buf), "%d book%s", count, count == 1 ? "" : "s");
    }

    return std::string(buf);
}

std::string books_read_count_i18n(int count) {
    char buf[96];

    if (g_lang == LANG_PL) {
        snprintf(buf, sizeof(buf), "Przeczytano %d %s", count, book_form_pl(count));
    } else {
        snprintf(buf, sizeof(buf), "%d book%s read", count, count == 1 ? "" : "s");
    }

    return std::string(buf);
}

std::string sessions_short_i18n(int count) {
    char buf[64];

    if (g_lang == LANG_PL) {
        snprintf(buf, sizeof(buf), "%d ses.", count);
    } else {
        snprintf(buf, sizeof(buf), "%d sess", count);
    }

    return std::string(buf);
}

std::string percent_read_i18n(int percent) {
    char buf[64];

    if (g_lang == LANG_PL) {
        snprintf(buf, sizeof(buf), "%d%% przeczytano", percent);
    } else {
        snprintf(buf, sizeof(buf), "%d%% read", percent);
    }

    return std::string(buf);
}

std::string more_count_i18n(int count) {
    char buf[64];

    if (g_lang == LANG_PL) {
        snprintf(buf, sizeof(buf), "+ %d więcej", count);
    } else {
        snprintf(buf, sizeof(buf), "+ %d more", count);
    }

    return std::string(buf);
}

std::string finished_with_sessions_i18n(int sessions) {
    return std::string(tr("Finished")) + "  ·  " + sessions_short_i18n(sessions);
}

std::string percent_read_with_sessions_i18n(int percent, int sessions) {
    return percent_read_i18n(percent) + "  ·  " + sessions_short_i18n(sessions);
}

namespace {

struct I18nAutoInit {
    I18nAutoInit() {
        i18n_init();
    }
};

I18nAutoInit g_i18n_auto_init;

} // namespace
