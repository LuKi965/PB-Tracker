#pragma once

#include <string>

void i18n_init();
bool i18n_is_pl();

const char *tr(const char *key);

const char *month_name_i18n(int month);
const char *weekday_short_i18n(int weekday);

std::string format_hms_i18n(long seconds);
std::string books_count_i18n(int count);
std::string books_read_count_i18n(int count);
std::string sessions_short_i18n(int count);
std::string percent_read_i18n(int percent);
std::string more_count_i18n(int count);
std::string finished_with_sessions_i18n(int sessions);
std::string percent_read_with_sessions_i18n(int percent, int sessions);
