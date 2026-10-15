#include "Log.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

namespace ms {

Redactor &Redactor::instance()
{
	static Redactor r;
	return r;
}

void Redactor::addSecret(const QString &secret)
{
	// Very short strings would redact ordinary words; keys are long.
	if (secret.trimmed().size() < 4)
		return;
	QMutexLocker l(&m_mutex);
	if (!m_secrets.contains(secret))
		m_secrets.append(secret);
}

void Redactor::removeSecret(const QString &secret)
{
	QMutexLocker l(&m_mutex);
	m_secrets.removeAll(secret);
}

QString Redactor::redact(const QString &text) const
{
	QString out = text;
	{
		QMutexLocker l(&m_mutex);
		for (const auto &s : m_secrets)
			out.replace(s, QStringLiteral("[REDACTED]"));
	}
	// Generic patterns: key=..., stream keys embedded in query strings.
	static const QStringList params = {"key=", "streamkey=", "token=", "auth="};
	for (const auto &p : params) {
		int idx = 0;
		while ((idx = out.indexOf(p, idx, Qt::CaseInsensitive)) >= 0) {
			const int start = idx + p.size();
			int end = start;
			while (end < out.size() && !out.at(end).isSpace() && out.at(end) != QLatin1Char('&') &&
			       out.at(end) != QLatin1Char('"'))
				++end;
			if (end > start)
				out.replace(start, end - start, QStringLiteral("[REDACTED]"));
			idx = start;
		}
	}
	return out;
}

Log &Log::instance()
{
	static Log l;
	return l;
}

void Log::prune(const QString &dir, int keep)
{
	QDir d(dir);
	const auto files = d.entryInfoList({"marshalls-*.log"}, QDir::Files, QDir::Name | QDir::Reversed);
	for (int i = keep; i < files.size(); ++i)
		QFile::remove(files.at(i).absoluteFilePath());
}

bool Log::open(const QString &dir)
{
	QMutexLocker l(&m_mutex);
	QDir().mkpath(dir);
	m_dir = dir;
	if (m_file)
		fclose(m_file);
	prune(dir, kMaxFiles - 1);
	m_path = dir + QStringLiteral("/marshalls-") + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") +
		 QStringLiteral(".log");
#ifdef _WIN32
	m_file = _wfopen(reinterpret_cast<const wchar_t *>(m_path.utf16()), L"ab");
#else
	m_file = fopen(QFile::encodeName(m_path).constData(), "ab");
#endif
	m_written = 0;
	m_capped = false;
	return m_file != nullptr;
}

void Log::close()
{
	QMutexLocker l(&m_mutex);
	if (m_file) {
		fclose(m_file);
		m_file = nullptr;
	}
}

void Log::write(LogLevel level, const QString &component, const QString &message)
{
	if (level < m_min)
		return;
	static const char *names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
	const QString line = QStringLiteral("%1 %2 [%3] %4\n")
				     .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"),
					  QLatin1String(names[int(level)]), component,
					  Redactor::instance().redact(message));
	const QByteArray utf8 = line.toUtf8();
	QMutexLocker l(&m_mutex);
	if (m_echo)
		fwrite(utf8.constData(), 1, size_t(utf8.size()), stderr);
	if (!m_file || m_capped)
		return;
	if (m_written + utf8.size() > kMaxBytes) {
		static const char capped[] = "-- log size limit reached; further lines dropped --\n";
		fwrite(capped, 1, sizeof(capped) - 1, m_file);
		fflush(m_file);
		m_capped = true;
		return;
	}
	fwrite(utf8.constData(), 1, size_t(utf8.size()), m_file);
	m_written += utf8.size();
	if (level >= LogLevel::Warning)
		fflush(m_file);
}

void logInfo(const QString &c, const QString &m)
{
	Log::instance().write(LogLevel::Info, c, m);
}
void logWarn(const QString &c, const QString &m)
{
	Log::instance().write(LogLevel::Warning, c, m);
}
void logError(const QString &c, const QString &m)
{
	Log::instance().write(LogLevel::Error, c, m);
}
void logDebug(const QString &c, const QString &m)
{
	Log::instance().write(LogLevel::Debug, c, m);
}

} // namespace ms
