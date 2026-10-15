#include "ShortcutMap.h"

#include <QKeySequence>

namespace ms {

const QVector<ShortcutAction> &ShortcutMap::catalogue()
{
	static const QVector<ShortcutAction> list = [] {
		QVector<ShortcutAction> l;
		l.append({"take.cut", QStringLiteral("Corte (colocar selecionada no ar)"), QStringLiteral("Programa"), "Return"});
		l.append({"take.auto", QStringLiteral("Transição automática"), QStringLiteral("Programa"), "Space"});
		l.append({"program.ftb", QStringLiteral("Fade para preto / voltar"), QStringLiteral("Programa"), "B"});
		for (int i = 1; i <= kSlots; ++i) {
			l.append({QStringLiteral("slot.select.%1").arg(i), QStringLiteral("Selecionar fonte %1").arg(i),
				  QStringLiteral("Fontes"), QString::number(i)});
			l.append({QStringLiteral("slot.take.%1").arg(i),
				  QStringLiteral("Colocar fonte %1 no ar (corte)").arg(i), QStringLiteral("Fontes"),
				  QStringLiteral("Ctrl+%1").arg(i)});
		}
		for (int i = 1; i <= kLowerThirdSlots; ++i)
			l.append({QStringLiteral("lt.toggle.%1").arg(i), QStringLiteral("Ativar/remover gráfico %1").arg(i),
				  QStringLiteral("Gráficos"), QStringLiteral("Alt+%1").arg(i)});
		l.append({"lt.clear", QStringLiteral("Remover todos os gráficos"), QStringLiteral("Gráficos"), "Alt+0"});
		for (int i = 1; i <= kMicSlots; ++i)
			l.append({QStringLiteral("mic.mute.%1").arg(i), QStringLiteral("Mute do canal de áudio %1").arg(i),
				  QStringLiteral("Áudio"), QStringLiteral("F%1").arg(i)});
		l.append({"media.playpause", QStringLiteral("Reproduzir/pausar mídia no ar"), QStringLiteral("Mídia"), "P"});
		l.append({"media.restart", QStringLiteral("Reiniciar mídia no ar"), QStringLiteral("Mídia"), "Shift+P"});
		l.append({"rec.toggle", QStringLiteral("Iniciar/encerrar gravação"), QStringLiteral("Saídas"), "Ctrl+R", true});
		l.append({"stream.toggle", QStringLiteral("Iniciar/encerrar transmissão"), QStringLiteral("Saídas"),
			  "Ctrl+Shift+L", true});
		return l;
	}();
	return list;
}

const ShortcutAction *ShortcutMap::find(const QString &id)
{
	for (const auto &a : catalogue())
		if (a.id == id)
			return &a;
	return nullptr;
}

QString ShortcutMap::normalize(const QString &seq)
{
	if (seq.trimmed().isEmpty())
		return {};
	const QKeySequence ks = QKeySequence::fromString(seq, QKeySequence::PortableText);
	if (ks.isEmpty() || ks[0].key() == Qt::Key_unknown)
		return {};
	// Only single-chord shortcuts are supported for live operation.
	return QKeySequence(ks[0]).toString(QKeySequence::PortableText);
}

bool ShortcutMap::isForbidden(const QString &seq, QString *why)
{
	static const QStringList forbidden = {"Alt+F4", "Ctrl+Q", "Ctrl+W", "Ctrl+F4", "Alt+Tab", "Ctrl+Esc",
					      "Ctrl+Alt+Del", "Meta+L", "Meta+D", "Meta+Tab", "Alt+Esc"};
	const QString n = normalize(seq);
	for (const auto &f : forbidden) {
		if (n.compare(normalize(f), Qt::CaseInsensitive) == 0) {
			if (why)
				*why = QStringLiteral("%1 é reservado pelo sistema ou fecha janelas").arg(n);
			return true;
		}
	}
	return false;
}

ShortcutMap::ShortcutMap()
{
	resetToDefaults();
}

void ShortcutMap::resetToDefaults()
{
	m_keys.clear();
	for (const auto &a : catalogue())
		m_keys.insert(a.id, normalize(a.defaultKey));
}

void ShortcutMap::load(const QMap<QString, QString> &overrides)
{
	resetToDefaults();
	for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
		if (!find(it.key()))
			continue; // unknown action from another version: ignore
		const QString n = normalize(it.value());
		if (!n.isEmpty() && isForbidden(n))
			continue;
		m_keys[it.key()] = n;
	}
}

QMap<QString, QString> ShortcutMap::overrides() const
{
	QMap<QString, QString> out;
	for (const auto &a : catalogue()) {
		const QString cur = m_keys.value(a.id);
		if (cur != normalize(a.defaultKey))
			out.insert(a.id, cur);
	}
	return out;
}

QString ShortcutMap::key(const QString &actionId) const
{
	return m_keys.value(actionId);
}

bool ShortcutMap::setKey(const QString &actionId, const QString &seq, QString *why)
{
	if (!find(actionId)) {
		if (why)
			*why = QStringLiteral("ação desconhecida");
		return false;
	}
	const QString n = normalize(seq);
	if (!n.isEmpty() && isForbidden(n, why))
		return false;
	m_keys[actionId] = n;
	return true;
}

QMap<QString, QStringList> ShortcutMap::conflicts() const
{
	QMap<QString, QStringList> byKey;
	for (auto it = m_keys.cbegin(); it != m_keys.cend(); ++it)
		if (!it.value().isEmpty())
			byKey[it.value()].append(it.key());
	QMap<QString, QStringList> out;
	for (auto it = byKey.cbegin(); it != byKey.cend(); ++it)
		if (it.value().size() > 1)
			out.insert(it.key(), it.value());
	return out;
}

QString ShortcutMap::actionForKey(const QString &seq) const
{
	const QString n = normalize(seq);
	if (n.isEmpty())
		return {};
	QString found;
	for (auto it = m_keys.cbegin(); it != m_keys.cend(); ++it) {
		if (it.value() == n) {
			if (!found.isEmpty())
				return {}; // conflicting binding: do nothing rather than guess
			found = it.key();
		}
	}
	return found;
}

} // namespace ms
