// Versioned, crash-safe persistence of studio profiles.
//
// * Writes are atomic (QSaveFile) and skipped when nothing changed, so a
//   valid file is never rewritten needlessly.
// * The previous valid file is kept as <profile>.json.bak; a corrupt main
//   file is recovered from it and quarantined as *.corrupt-<timestamp>.
// * Older schema versions are migrated step by step; a copy of the original
//   is kept as <profile>.json.v<N>.bak before the first migrated save.
#pragma once

#include "StudioConfig.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace ms {

class ConfigStore {
public:
	explicit ConfigStore(const QString &rootDir);

	QString rootDir() const { return m_root; }
	QString profilesDir() const;
	QString profilePath(const QString &profileName) const;

	QStringList listProfiles() const;

	struct LoadResult {
		StudioProfile profile;
		bool fromDisk = false;
		bool migrated = false;
		bool recoveredFromBackup = false;
		bool newerSchema = false;
		QString message; // human readable notes for the operator / log
	};
	LoadResult load(const QString &profileName) const;

	// Returns true on success (also when skipped because content is identical).
	bool save(const StudioProfile &profile, QString *error = nullptr) const;
	bool removeProfile(const QString &profileName, QString *error = nullptr) const;
	bool renameProfile(const QString &from, const QString &to, QString *error = nullptr) const;

	QString lastProfile() const;
	void setLastProfile(const QString &name) const;

	static QString sanitize(const QString &profileName);
	static QJsonObject migrate(QJsonObject doc, QStringList *notes);
	static QByteArray serialize(const StudioProfile &p);

private:
	QString m_root;
};

} // namespace ms
