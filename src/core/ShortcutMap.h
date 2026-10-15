// Keyboard shortcut catalogue, defaults and conflict detection.
#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ms {

struct ShortcutAction {
	QString id;
	QString label;
	QString category;
	QString defaultKey; // portable text, e.g. "Ctrl+R"
	bool needsConfirmWhenStopping = false;
};

class ShortcutMap {
public:
	static constexpr int kSlots = 9;
	static constexpr int kLowerThirdSlots = 9;
	static constexpr int kMicSlots = 8;

	static const QVector<ShortcutAction> &catalogue();
	static const ShortcutAction *find(const QString &id);

	// Normalizes a key sequence to portable text ("" when invalid/empty).
	static QString normalize(const QString &seq);
	// Sequences the application refuses to bind (close app, OS shortcuts…).
	static bool isForbidden(const QString &seq, QString *why = nullptr);

	ShortcutMap();
	void load(const QMap<QString, QString> &overrides);
	QMap<QString, QString> overrides() const; // only entries different from defaults

	QString key(const QString &actionId) const;
	// Returns false when forbidden; conflicts are allowed but reported.
	bool setKey(const QString &actionId, const QString &seq, QString *why = nullptr);
	void resetToDefaults();

	// key → action ids, only keys bound to more than one action
	QMap<QString, QStringList> conflicts() const;
	QString actionForKey(const QString &seq) const; // empty if none or conflicting

private:
	QMap<QString, QString> m_keys;
};

} // namespace ms
