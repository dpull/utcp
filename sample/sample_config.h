#pragma once

enum class log_level
{
	NoLogging = 0,
	Fatal,
	Error,
	Warning,
	Display,
	Log,
	Verbose,
};
void log(log_level level, const char* fmt, ...);

struct sample_config
{
	log_level log_level_limit;
	int outgoing_loss;
};

extern sample_config* g_config;
