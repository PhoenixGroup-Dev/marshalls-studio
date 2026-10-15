#include "SecretStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif

namespace ms {

namespace {

#ifdef _WIN32
QByteArray protect(const QByteArray &plain, bool encrypt)
{
	DATA_BLOB in{DWORD(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
	DATA_BLOB out{};
	BOOL ok = encrypt ? CryptProtectData(&in, L"Marshalls Studio", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
			  : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
	if (!ok)
		return {};
	QByteArray result(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
	LocalFree(out.pbData);
	return result;
}
#else
QByteArray protect(const QByteArray &data, bool /*encrypt*/)
{
	return data;
}
#endif

} // namespace

SecretStore::SecretStore(const QString &dir) : m_dir(dir)
{
	QDir().mkpath(m_dir);
}

bool SecretStore::isEncrypted()
{
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

QString SecretStore::pathFor(const QString &name) const
{
	const QByteArray h = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
	return m_dir + QStringLiteral("/") + QString::fromLatin1(h) + QStringLiteral(".secret");
}

bool SecretStore::set(const QString &name, const QString &secret, QString *error)
{
	if (secret.isEmpty())
		return remove(name) || true;
	const QByteArray blob = protect(secret.toUtf8(), true);
	if (blob.isEmpty()) {
		if (error)
			*error = QStringLiteral("falha ao proteger o segredo");
		return false;
	}
	QSaveFile f(pathFor(name));
	if (!f.open(QIODevice::WriteOnly)) {
		if (error)
			*error = f.errorString();
		return false;
	}
	f.write(blob.toBase64());
	if (!f.commit()) {
		if (error)
			*error = f.errorString();
		return false;
	}
	QFile::setPermissions(pathFor(name), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	return true;
}

QString SecretStore::get(const QString &name) const
{
	QFile f(pathFor(name));
	if (!f.open(QIODevice::ReadOnly))
		return {};
	return QString::fromUtf8(protect(QByteArray::fromBase64(f.readAll()), false));
}

bool SecretStore::remove(const QString &name)
{
	return QFile::remove(pathFor(name));
}

} // namespace ms
