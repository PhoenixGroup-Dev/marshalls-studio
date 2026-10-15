// Storage for stream keys outside the profile JSON.
//
// Windows: encrypted with DPAPI (CryptProtectData, current user scope).
// Other platforms: stored in a user-only (0600) file, base64 encoded — this
// is NOT encryption; documented as a known limitation for non-Windows builds.
#pragma once

#include <QString>

namespace ms {

class SecretStore {
public:
	explicit SecretStore(const QString &dir);
	bool set(const QString &name, const QString &secret, QString *error = nullptr);
	QString get(const QString &name) const;
	bool remove(const QString &name);
	static bool isEncrypted();

private:
	QString pathFor(const QString &name) const;
	QString m_dir;
};

} // namespace ms
