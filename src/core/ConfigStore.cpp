#include "ConfigStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>

namespace ms {

namespace {

bool readJson(const QString &path, QJsonObject *out, QString *err)
{
	QFile f(path);
	if (!f.exists()) {
		*err = QStringLiteral("arquivo inexistente");
		return false;
	}
	if (!f.open(QIODevice::ReadOnly)) {
		*err = f.errorString();
		return false;
	}
	QJsonParseError pe;
	const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
	if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
		*err = pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("raiz não é um objeto");
		return false;
	}
	*out = doc.object();
	return true;
}

bool writeAtomic(const QString &path, const QByteArray &data, QString *error)
{
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		if (error)
			*error = f.errorString();
		return false;
	}
	if (f.write(data) != data.size()) {
		if (error)
			*error = f.errorString();
		f.cancelWriting();
		return false;
	}
	if (!f.commit()) {
		if (error)
			*error = f.errorString();
		return false;
	}
	return true;
}

} // namespace

ConfigStore::ConfigStore(const QString &rootDir) : m_root(rootDir)
{
	QDir().mkpath(profilesDir());
}

QString ConfigStore::profilesDir() const
{
	return m_root + QStringLiteral("/profiles");
}

QString ConfigStore::sanitize(const QString &profileName)
{
	QString out;
	for (const QChar c : profileName.trimmed()) {
		if (c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('-') || c == QLatin1Char('_'))
			out.append(c);
		else
			out.append(QLatin1Char('_'));
	}
	out = out.left(64).trimmed();
	return out.isEmpty() ? QStringLiteral("Perfil") : out;
}

QString ConfigStore::profilePath(const QString &profileName) const
{
	return profilesDir() + QLatin1Char('/') + sanitize(profileName) + QStringLiteral(".json");
}

QStringList ConfigStore::listProfiles() const
{
	QStringList names;
	const auto files = QDir(profilesDir()).entryInfoList({"*.json"}, QDir::Files, QDir::Name);
	for (const QFileInfo &fi : files) {
		QJsonObject o;
		QString err;
		if (readJson(fi.absoluteFilePath(), &o, &err)) {
			const QString n = o.value("name").toString();
			names.append(n.isEmpty() ? fi.completeBaseName() : n);
		} else {
			names.append(fi.completeBaseName());
		}
	}
	return names;
}

QJsonObject ConfigStore::migrate(QJsonObject doc, QStringList *notes)
{
	int version = doc.value("schema_version").toInt(1);

	// v1 (pre-release development format): linear volume (0..1) per source and
	// a flat "transition_ms". v2 stores the fader in dB and a transition block.
	if (version < 2) {
		QJsonArray sources = doc.value("sources").toArray();
		for (int i = 0; i < sources.size(); ++i) {
			QJsonObject s = sources.at(i).toObject();
			QJsonObject audio = s.value("audio").toObject();
			if (audio.contains("volume") && !audio.contains("volume_db")) {
				const double lin = audio.value("volume").toDouble(1.0);
				audio.insert("volume_db", lin > 0.0 ? 20.0 * std::log10(lin) : -100.0);
				audio.remove("volume");
			}
			s.insert("audio", audio);
			sources.replace(i, s);
		}
		doc.insert("sources", sources);
		if (doc.contains("transition_ms") && !doc.contains("transition")) {
			doc.insert("transition", QJsonObject{{"type", "fade"}, {"duration_ms", doc.value("transition_ms")}});
			doc.remove("transition_ms");
		}
		if (notes)
			notes->append(QStringLiteral("Configuração migrada do formato 1 para o formato 2."));
		version = 2;
	}

	doc.insert("schema_version", version);
	return doc;
}

QByteArray ConfigStore::serialize(const StudioProfile &p)
{
	return QJsonDocument(p.toJson()).toJson(QJsonDocument::Indented);
}

ConfigStore::LoadResult ConfigStore::load(const QString &profileName) const
{
	LoadResult r;
	const QString path = profilePath(profileName);
	QJsonObject doc;
	QString err;
	QStringList notes;

	if (readJson(path, &doc, &err)) {
		r.fromDisk = true;
	} else if (QFile::exists(path)) {
		// Main file unreadable: quarantine it and try the backup.
		const QString quarantine =
			path + QStringLiteral(".corrupt-") + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
		QFile::rename(path, quarantine);
		notes.append(QStringLiteral("Arquivo de perfil corrompido (%1); movido para %2.")
				     .arg(err, QFileInfo(quarantine).fileName()));
		QString berr;
		if (readJson(path + QStringLiteral(".bak"), &doc, &berr)) {
			r.fromDisk = true;
			r.recoveredFromBackup = true;
			notes.append(QStringLiteral("Configuração recuperada da cópia de segurança."));
		} else {
			notes.append(QStringLiteral("Sem cópia de segurança válida; iniciando perfil vazio."));
		}
	}

	if (r.fromDisk) {
		const int ver = doc.value("schema_version").toInt(1);
		if (ver > kSchemaVersion) {
			r.newerSchema = true;
			notes.append(QStringLiteral("Perfil criado por uma versão mais nova (formato %1). "
						    "Uma cópia será preservada antes de qualquer gravação.")
					     .arg(ver));
			const QString keep = path + QStringLiteral(".v%1.bak").arg(ver);
			if (!QFile::exists(keep))
				QFile::copy(path, keep);
		} else if (ver < kSchemaVersion) {
			const QString keep = path + QStringLiteral(".v%1.bak").arg(ver);
			if (!QFile::exists(keep))
				QFile::copy(path, keep);
			doc = migrate(doc, &notes);
			r.migrated = true;
		}
		r.profile = StudioProfile::fromJson(doc);
		r.profile.schemaVersion = kSchemaVersion;
	}
	if (r.profile.name.isEmpty() || !r.fromDisk)
		r.profile.name = profileName;
	r.message = notes.join(QLatin1Char(' '));
	return r;
}

bool ConfigStore::save(const StudioProfile &profile, QString *error) const
{
	QDir().mkpath(profilesDir());
	const QString path = profilePath(profile.name);
	const QByteArray data = serialize(profile);

	QFile existing(path);
	if (existing.open(QIODevice::ReadOnly)) {
		const QByteArray old = existing.readAll();
		existing.close();
		if (old == data)
			return true; // unchanged: never touch a valid file
		// Keep the previous version only if it is valid JSON.
		QJsonParseError pe;
		QJsonDocument::fromJson(old, &pe);
		if (pe.error == QJsonParseError::NoError) {
			QString berr;
			if (!writeAtomic(path + QStringLiteral(".bak"), old, &berr) && error)
				*error = QStringLiteral("backup: ") + berr;
		}
	}
	return writeAtomic(path, data, error);
}

bool ConfigStore::removeProfile(const QString &profileName, QString *error) const
{
	const QString path = profilePath(profileName);
	if (!QFile::exists(path)) {
		if (error)
			*error = QStringLiteral("perfil inexistente");
		return false;
	}
	// Removal keeps a recoverable copy.
	const QString trash = path + QStringLiteral(".deleted-") +
			      QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
	return QFile::rename(path, trash);
}

bool ConfigStore::renameProfile(const QString &from, const QString &to, QString *error) const
{
	LoadResult r = load(from);
	if (!r.fromDisk) {
		if (error)
			*error = QStringLiteral("perfil de origem inexistente");
		return false;
	}
	if (sanitize(from) == sanitize(to)) {
		r.profile.name = to;
		return save(r.profile, error);
	}
	if (QFile::exists(profilePath(to))) {
		if (error)
			*error = QStringLiteral("já existe um perfil com esse nome");
		return false;
	}
	r.profile.name = to;
	if (!save(r.profile, error))
		return false;
	return QFile::remove(profilePath(from));
}

QString ConfigStore::lastProfile() const
{
	QJsonObject o;
	QString err;
	if (readJson(m_root + QStringLiteral("/app.json"), &o, &err))
		return o.value("last_profile").toString();
	return {};
}

void ConfigStore::setLastProfile(const QString &name) const
{
	QJsonObject o{{"last_profile", name}};
	writeAtomic(m_root + QStringLiteral("/app.json"), QJsonDocument(o).toJson(), nullptr);
}

} // namespace ms
