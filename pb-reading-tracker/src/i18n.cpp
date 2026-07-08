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
    {"Reading Stats", "Statystyki czytania"},
    {"Overview", "Podsumowanie"},
    {"Today", "Dzisiaj"},
    {"This Week", "Ten tydzień"},
    {"Calendar", "Kalendarz"},
    {"Books", "Książki"},
    {"Library", "Biblioteka"},
    {"Reading Life", "Statystyki czytania"},
    {"Hours", "Godziny"},
    {"Total Hours Read", "Łączny czas czytania"},
    {"Books Finished", "Ukończone książki"},
    {"Sessions", "Sesje"},
    {"Day Streak", "Seria dni"},
    {"This Month", "Ten miesiąc"},
    {"This Year", "Ten rok"},
    {"Recent Books", "Ostatnie książki"},
    {"Finished", "Ukończone"},
    {"Total Hours", "Łączny czas"},
    {"Total read", "Łączny czas"},
    {"Currently Reading", "AKTUALNIE CZYTASZ"},
    {"Book progress", "Postęp książki"},
    {"PocketBook progress", "Postęp PocketBooka"},
    {"Tracked time", "Czas śledzony"},
    {"Progress source: PocketBook", "Postęp: PocketBook"},
    {"Time source: Reading Stats", "Czas: Reading Stats"},
    {"Started before Reading Stats", "Rozpoczęte przed Reading Stats"},
    {"Native progress, no tracked time yet", "Postęp z PocketBooka, brak śledzonego czasu"},
    {"Entire Library", "CAŁA BIBLIOTEKA"},
    {"Reading profile", "PROFIL CZYTANIA"},
    {"Books tracked", "Książki w bazie"},
    {"Known books", "Znane książki"},
    {"In progress", "W trakcie"},
    {"Tracked", "Śledzone"},
    {"Tracked books", "Śledzone książki"},
    {"Recently tracked", "OSTATNIO ŚLEDZONE"},
    {"Last 14 days", "Ostatnie 14 dni"},
    {"Reading days", "Dni czytania"},
    {"Best day", "Najlepszy dzień"},
    {"Avg min/session", "Śr. min/sesja"},
    {"of your library is complete", "biblioteki ukończone"},
    {"of known books are complete", "znanych książek ukończone"},
    {"Recent activity", "OSTATNIA AKTYWNOŚĆ"},
    {"Activity", "Aktywność"},
    {"History", "Historia"},
    {"Safe USB mode: background tracking is off", "Tryb bezpieczny USB: śledzenie w tle wyłączone"},
    {"No reading data yet.", "Brak danych czytania."},
    {"Open a book and come back after a few minutes.", "Otwórz książkę i wróć po kilku minutach."},
    {"No books recorded yet. Start reading!", "Brak zapisanych książek. Zacznij czytać!"},
    {"No books logged in this period.", "Brak książek w tym okresie."},
    {"Darker = more time read", "Ciemniej = więcej czytania"},
    {"Database Error", "Błąd bazy danych"},
    {"Failed to open or initialize the database reading_stats.db.", "Nie udało się otworzyć lub zainicjalizować bazy reading_stats.db."},
    {"No books yet", "Brak książek"},
    {"App touch zones are left to PocketBook settings", "Strefy dotyku obsługuje konfiguracja PocketBooka"},
    {"Use page turn buttons or configured page zones", "Użyj przycisków zmiany stron albo własnych stref PocketBooka"},
    {"(untitled)", "(bez tytułu)"},
    {NULL, NULL}
};

const char *MONTHS_EN[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

const char *MONTHS_PL[] = {
    "sty", "lut", "mar", "kwi", "maj", "cze",
    "lip", "sie", "wrz", "paź", "lis", "gru"
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

const char *session_form_pl(int count) {
    int n10 = count % 10;
    int n100 = count % 100;

    if (count == 1) return "sesja";
    if (n10 >= 2 && n10 <= 4 && !(n100 >= 12 && n100 <= 14)) return "sesje";
    return "sesji";
}

} // namespace

void i18n_init() {
    g_lang = LANG_EN;

    char lang[32] = {0};
    if (read_language_from_file(FLASHDIR "/system/pbreadstats/config.cfg", lang, sizeof(lang))) {
        set_language_from_code(lang);
        return;
    }

    if (read_language_from_file(CONFIGPATH "/global.cfg", lang, sizeof(lang))) {
        set_language_from_code(lang);
        return;
    }

    if (read_language_from_file(GLOBALCONFIGFILE, lang, sizeof(lang))) {
        set_language_from_code(lang);
        return;
    }
}

bool i18n_is_pl() {
    return g_lang == LANG_PL;
}

const char *tr(const char *key) {
    if (g_lang == LANG_PL) {
        for (int i = 0; TRANSLATIONS[i].key; i++) {
            if (strcmp(TRANSLATIONS[i].key, key) == 0) return TRANSLATIONS[i].pl;
        }
    }
    return key;
}

const char *month_name_i18n(int month) {
    if (month < 1 || month > 12) return "";
    return g_lang == LANG_PL ? MONTHS_PL[month - 1] : MONTHS_EN[month - 1];
}

const char *weekday_short_i18n(int weekday) {
    static const char *EN[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *PL[] = {"nd", "pn", "wt", "śr", "cz", "pt", "sb"};
    if (weekday < 0 || weekday > 6) weekday = 0;
    return g_lang == LANG_PL ? PL[weekday] : EN[weekday];
}

std::string format_duration_i18n(long seconds) {
    long minutes = seconds / 60;
    if (minutes < 60) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld min", minutes);
        return buf;
    }

    long hours = minutes / 60;
    long rem = minutes % 60;
    char buf[64];
    if (rem == 0) snprintf(buf, sizeof(buf), "%ld h", hours);
    else snprintf(buf, sizeof(buf), "%ld h %ld min", hours, rem);
    return buf;
}

std::string books_count_i18n(int count) {
    char buf[64];
    if (g_lang == LANG_PL) snprintf(buf, sizeof(buf), "%d %s", count, book_form_pl(count));
    else snprintf(buf, sizeof(buf), "%d book%s", count, count == 1 ? "" : "s");
    return buf;
}

std::string books_read_count_i18n(int count) {
    return books_count_i18n(count);
}

std::string sessions_count_i18n(int count) {
    char buf[64];
    if (g_lang == LANG_PL) snprintf(buf, sizeof(buf), "%d %s", count, session_form_pl(count));
    else snprintf(buf, sizeof(buf), "%d session%s", count, count == 1 ? "" : "s");
    return buf;
}

std::string sessions_short_i18n(int count) {
    char buf[32];
    if (g_lang == LANG_PL) snprintf(buf, sizeof(buf), "%d ses.", count);
    else snprintf(buf, sizeof(buf), "%d ses.", count);
    return buf;
}

std::string percent_read_i18n(int percent) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%% %s", percent, g_lang == LANG_PL ? "przeczytano" : "read");
    return buf;
}

std::string month_year_i18n(int year, int month) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %d", month_name_i18n(month), year);
    return buf;
}
