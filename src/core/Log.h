// Rotating session log with secret redaction.
//
// One file per session (marshalls-YYYYMMDD-HHMMSS.log), the newest
// kMaxFiles are kept, each file is capped at kMaxBytes. Every line passes
// through the Redactor so stream keys never reach disk.
#pragma once

#include <QMutex>
#include <QString>
#include <QStringList>
#include <cstdio>

namespace ms {

class Redactor {
public:
	static Redactor &instance();
	void addSecret(const QString &secret);
	void removeSecret(const QString &secret);
	QString redact(const QString &text) const;

private:
	mutable QMutex m_mutex;
	QStringList m_secrets;
};

enum class LogLevel { Debug, Info, Warning, Error };

class Log {
public:
	static constexpr int kMaxFiles = 10;
	static constexpr qint64 kMaxBytes = 32 * 1024 * 1024;

	static Log &instance();
	bool open(const QString &dir); // starts a new session file and prunes old ones
	void close();
	void write(LogLevel level, const QString &component, const QString &message);
	QString currentPath() const { return m_path; }
	QString directory() const { return m_dir; }
	void setEchoToStderr(bool on) { m_echo = on; }
	void setMinimumLevel(LogLevel l) { m_min = l; }

	static void prune(const QString &dir, int keep);

private:
	QMutex m_mutex;
	FILE *m_file = nullptr;
	qint64 m_written = 0;
	bool m_capped = false;
	bool m_echo = true;
	LogLevel m_min = LogLevel::Info;
	QString m_path, m_dir;
};

void logInfo(const QString &component, const QString &msg);
void logWarn(const QString &component, const QString &msg);
void logError(const QString &component, const QString &msg);
void logDebug(const QString &component, const QString &msg);

} // namespace ms
