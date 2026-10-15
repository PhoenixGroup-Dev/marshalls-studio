#include "MainWindow.h"

#include "AudioMixer.h"
#include "ControlPanel.h"
#include "GraphicsDialogs.h"
#include "GraphicsPanel.h"
#include "Log.h"
#include "ProgramMonitor.h"
#include "SceneEditor.h"
#include "SettingsDialog.h"
#include "SourceDialogs.h"
#include "SourceGrid.h"
#include "Studio.h"
#include "Theme.h"
#include "TopBar.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef _WIN32
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

namespace ms {

MainWindow::MainWindow(Studio *studio, QWidget *parent) : QMainWindow(parent), m_studio(studio)
{
	setWindowTitle(QStringLiteral("MARSHALLS STUDIO"));
	setMinimumSize(1100, 680);
	resize(1600, 960);

	auto *central = new QWidget;
	auto *v = new QVBoxLayout(central);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);

	m_top = new TopBar(studio);
	v->addWidget(m_top);

	m_monitor = new ProgramMonitor(studio);
	m_controls = new ControlPanel(studio);
	m_graphics = new GraphicsPanel(studio);
	m_grid = new SourceGrid(studio);
	m_mixer = new AudioMixer(studio);

	m_rightCol = new QSplitter(Qt::Vertical);
	m_rightCol->addWidget(panel(m_controls));
	m_rightCol->addWidget(panel(m_graphics));
	m_rightCol->setStretchFactor(0, 0);
	m_rightCol->setStretchFactor(1, 1);

	m_topRow = new QSplitter(Qt::Horizontal);
	m_topRow->addWidget(panel(m_monitor));
	m_topRow->addWidget(m_rightCol);
	m_topRow->setStretchFactor(0, 1);
	m_topRow->setStretchFactor(1, 0);

	m_bottomRow = new QSplitter(Qt::Horizontal);
	m_bottomRow->addWidget(panel(m_grid));
	m_bottomRow->addWidget(panel(m_mixer));
	m_bottomRow->setStretchFactor(0, 1);
	m_bottomRow->setStretchFactor(1, 0);

	m_vertical = new QSplitter(Qt::Vertical);
	m_vertical->addWidget(m_topRow);
	m_vertical->addWidget(m_bottomRow);
	m_vertical->setStretchFactor(0, 1);
	m_vertical->setStretchFactor(1, 1);
	for (QSplitter *s : {m_vertical, m_topRow, m_rightCol, m_bottomRow}) {
		s->setHandleWidth(4);
		s->setChildrenCollapsible(false);
	}
	auto *body = new QWidget;
	auto *bl = new QVBoxLayout(body);
	bl->setContentsMargins(4, 4, 4, 4);
	bl->addWidget(m_vertical);
	v->addWidget(body, 1);

	m_alerts = new AlertBar(studio);
	v->addWidget(m_alerts);
	setCentralWidget(central);

	m_layoutTimer = new QTimer(this);
	m_layoutTimer->setSingleShot(true);
	m_layoutTimer->setInterval(600);
	connect(m_layoutTimer, &QTimer::timeout, this, &MainWindow::saveLayout);
	for (QSplitter *s : {m_vertical, m_topRow, m_rightCol, m_bottomRow})
		connect(s, &QSplitter::splitterMoved, m_layoutTimer, qOverload<>(&QTimer::start));

	connect(m_top, &TopBar::recordToggleRequested, this, &MainWindow::toggleRecording);
	connect(m_top, &TopBar::streamToggleRequested, this, &MainWindow::toggleStreaming);
	connect(m_top, &TopBar::settingsRequested, this, &MainWindow::openSettings);
	connect(m_top, &TopBar::profileSwitchRequested, this, &MainWindow::switchProfile);
	connect(m_top, &TopBar::newProfileRequested, this, &MainWindow::newProfile);
	connect(m_top, &TopBar::openRecordingsRequested, this, &MainWindow::openRecordings);
	connect(m_grid, &SourceGrid::addRequested, this, &MainWindow::addContent);
	connect(m_grid, &SourceGrid::propertiesRequested, this, &MainWindow::editContent);
	connect(m_grid, &SourceGrid::removeRequested, this, &MainWindow::removeContent);
	connect(m_grid, &SourceGrid::renameRequested, this, &MainWindow::renameContent);
	connect(m_grid, &SourceGrid::duplicateRequested, this, [this](const ContentRef &r) {
		const QString uid = m_studio->duplicateScene(r.uid);
		if (!uid.isEmpty())
			editContent(ContentRef::scene(uid));
	});
	connect(m_mixer, &AudioMixer::settingsRequested, this, &MainWindow::editAudio);
	connect(m_mixer, &AudioMixer::addRequested, this, &MainWindow::addContent);
	connect(m_graphics, &GraphicsPanel::editLowerThird, this, [this](const QString &uid) {
		LowerThirdDialog dlg(m_studio, uid, this);
		dlg.exec();
	});
	connect(m_graphics, &GraphicsPanel::editOverlay, this, [this](const QString &uid) {
		OverlayDialog dlg(m_studio, uid, this);
		dlg.exec();
	});
	connect(studio, &Studio::profileLoaded, this, [this] {
		setWindowTitle(QStringLiteral("MARSHALLS STUDIO — %1").arg(m_studio->profile().name));
		restoreLayout();
	});
	connect(studio, &Studio::settingsChanged, this, &MainWindow::applyCaptureExclusion);
	setWindowTitle(QStringLiteral("MARSHALLS STUDIO — %1").arg(studio->profile().name));
	qApp->installEventFilter(this);
	restoreLayout();
}

QWidget *MainWindow::panel(QWidget *content)
{
	auto *f = new QFrame;
	f->setObjectName("Panel");
	auto *l = new QVBoxLayout(f);
	l->setContentsMargins(0, 0, 0, 0);
	l->addWidget(content);
	return f;
}

void MainWindow::restoreLayout()
{
	const auto &st = m_studio->profile().ui.layoutState;
	bool restored = false;
	if (st.contains("vertical"))
		restored |= m_vertical->restoreState(st.value("vertical"));
	if (st.contains("top"))
		m_topRow->restoreState(st.value("top"));
	if (st.contains("right"))
		m_rightCol->restoreState(st.value("right"));
	if (st.contains("bottom"))
		m_bottomRow->restoreState(st.value("bottom"));
	if (!restored) {
		m_vertical->setSizes({560, 380});
		m_topRow->setSizes({1100, 380});
		m_rightCol->setSizes({300, 260});
		m_bottomRow->setSizes({1100, 380});
	}
	if (st.contains("window"))
		restoreGeometry(st.value("window"));
}

void MainWindow::saveLayout()
{
	UiSettings u = m_studio->profile().ui;
	u.layoutState.insert("vertical", m_vertical->saveState());
	u.layoutState.insert("top", m_topRow->saveState());
	u.layoutState.insert("right", m_rightCol->saveState());
	u.layoutState.insert("bottom", m_bottomRow->saveState());
	u.layoutState.insert("window", saveGeometry());
	m_studio->setUiSettings(u);
}

void MainWindow::showEvent(QShowEvent *e)
{
	QMainWindow::showEvent(e);
	applyCaptureExclusion();
}

void MainWindow::applyCaptureExclusion()
{
#ifdef _WIN32
	// Windows 10 2004+: the window is omitted from screen capture APIs (DXGI,
	// WGC, BitBlt), preventing the program from capturing itself.
	const DWORD affinity = m_studio->profile().ui.hideFromCapture ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
	if (!SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), affinity))
		logWarn("ui", QStringLiteral("SetWindowDisplayAffinity indisponível (requer Windows 10 2004+)"));
#endif
}

void MainWindow::showStartupNotes(const QStringList &notes)
{
	for (const QString &n : notes)
		if (!n.trimmed().isEmpty())
			m_alerts->push(1, n);
}

// ---------------------------------------------------------------------------
// keyboard
// ---------------------------------------------------------------------------

bool MainWindow::eventFilter(QObject *o, QEvent *e)
{
	if (e->type() == QEvent::KeyPress && isActiveWindow()) {
		auto *ke = static_cast<QKeyEvent *>(e);
		if (handleShortcut(ke))
			return true;
	}
	return QMainWindow::eventFilter(o, e);
}

bool MainWindow::handleShortcut(QKeyEvent *e)
{
	if (e->isAutoRepeat())
		return false;
	// Never steal keys from text entry or from dialogs.
	QWidget *focus = QApplication::focusWidget();
	if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
		return false;
	if (focus && (qobject_cast<QLineEdit *>(focus) || qobject_cast<QAbstractSpinBox *>(focus) ||
		      qobject_cast<QKeySequenceEdit *>(focus) || qobject_cast<QTextEdit *>(focus) ||
		      qobject_cast<QPlainTextEdit *>(focus) || (qobject_cast<QComboBox *>(focus) && static_cast<QComboBox *>(focus)->isEditable())))
		return false;
	const int key = e->key();
	if (key == Qt::Key_unknown || key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt ||
	    key == Qt::Key_Meta)
		return false;
	Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	// Shift+digit produces symbols on most layouts: map back to the digit.
	int k = key;
	if (e->nativeVirtualKey() >= '0' && e->nativeVirtualKey() <= '9' && (mods & Qt::ShiftModifier))
		k = Qt::Key_0 + int(e->nativeVirtualKey() - '0');
	if (key == Qt::Key_Enter)
		k = Qt::Key_Return;
	const QString seq = QKeySequence(QKeyCombination(mods, Qt::Key(k))).toString(QKeySequence::PortableText);
	const QString action = m_studio->shortcuts().actionForKey(seq);
	if (action.isEmpty())
		return false;
	runAction(action);
	return true;
}

void MainWindow::runAction(const QString &id)
{
	auto slotOf = [&](const QString &prefix) { return id.mid(prefix.size()).toInt(); };
	if (id == QLatin1String("take.cut"))
		m_studio->take(TakeMode::Cut);
	else if (id == QLatin1String("take.auto"))
		m_studio->take(TakeMode::Auto);
	else if (id == QLatin1String("program.ftb"))
		m_studio->toggleFadeToBlack();
	else if (id.startsWith(QLatin1String("slot.select.")))
		m_studio->selectSlot(slotOf("slot.select."));
	else if (id.startsWith(QLatin1String("slot.take.")))
		m_studio->takeSlot(slotOf("slot.take."), TakeMode::Cut);
	else if (id.startsWith(QLatin1String("lt.toggle.")))
		m_studio->toggleLowerThirdSlot(slotOf("lt.toggle."));
	else if (id == QLatin1String("lt.clear"))
		m_studio->clearGraphics();
	else if (id.startsWith(QLatin1String("mic.mute."))) {
		const int n = slotOf("mic.mute.");
		const auto ch = m_studio->audioChannels();
		if (n >= 1 && n <= ch.size())
			m_studio->toggleMute(ch[n - 1]);
	} else if (id == QLatin1String("media.playpause") || id == QLatin1String("media.restart")) {
		const ContentRef p = m_studio->program().program();
		const SourceConfig *s = p.type == ContentRef::Type::Source ? m_studio->profile().findSource(p.uid) : nullptr;
		if (s && s->kind == SourceKind::Media) {
			if (id == QLatin1String("media.restart"))
				m_studio->engine().mediaRestart(s->uid);
			else
				m_studio->engine().mediaPlayPause(s->uid);
		}
	} else if (id == QLatin1String("rec.toggle"))
		toggleRecording();
	else if (id == QLatin1String("stream.toggle"))
		toggleStreaming();
}

// ---------------------------------------------------------------------------
// content actions
// ---------------------------------------------------------------------------

void MainWindow::addContent(int kind)
{
	if (kind < 0) {
		SceneConfig sc;
		sc.name = QStringLiteral("Composição");
		// Start with the selected source (if any) so the editor is not empty.
		const ContentRef sel = m_studio->program().selected();
		if (sel.type == ContentRef::Type::Source)
			sc.items.append(SceneItemConfig{sel.uid});
		const QString uid = m_studio->addScene(sc);
		SceneEditorDialog dlg(m_studio, uid, this);
		dlg.exec();
		return;
	}
	AddSourceDialog dlg(m_studio, SourceKind(kind), this);
	if (dlg.exec() == QDialog::Accepted) {
		const SourceConfig c = dlg.result();
		if (kindHasVideo(c.kind))
			m_studio->select(ContentRef::source(c.uid));
	}
}

void MainWindow::editContent(const ContentRef &ref)
{
	if (ref.type == ContentRef::Type::Scene) {
		SceneEditorDialog dlg(m_studio, ref.uid, this);
		dlg.exec();
		return;
	}
	const SourceConfig *s = m_studio->profile().findSource(ref.uid);
	if (!s)
		return;
	if (s->kind == SourceKind::AudioInput || s->kind == SourceKind::AudioOutput) {
		editAudio(ref.uid);
		return;
	}
	SourcePropertiesDialog dlg(m_studio, ref.uid, this);
	dlg.exec();
}

void MainWindow::editAudio(const QString &uid)
{
	AudioChannelDialog dlg(m_studio, uid, this);
	dlg.exec();
}

void MainWindow::removeContent(const ContentRef &ref)
{
	const QString name = m_studio->contentName(ref);
	const bool onAir = m_studio->isOnProgram(ref);
	QString text = ref.type == ContentRef::Type::Scene
			       ? QStringLiteral("Excluir a composição “%1”? As fontes continuam na grade.").arg(name)
			       : QStringLiteral("Remover a fonte “%1”? Ela também sai de todas as composições.").arg(name);
	if (onAir)
		text = QStringLiteral("“%1” ESTÁ NO AR.\n\nRemovê-la deixará a saída em preto%2.\n\nContinuar?")
			       .arg(name, m_studio->isStreaming() || m_studio->isRecording()
						  ? QStringLiteral(" durante a gravação/transmissão")
						  : QString());
	QMessageBox box(onAir ? QMessageBox::Warning : QMessageBox::Question, QStringLiteral("Confirmar"), text,
			QMessageBox::Yes | QMessageBox::Cancel, this);
	box.setDefaultButton(QMessageBox::Cancel);
	box.button(QMessageBox::Yes)->setText(ref.type == ContentRef::Type::Scene ? QStringLiteral("Excluir")
										   : QStringLiteral("Remover"));
	if (box.exec() != QMessageBox::Yes)
		return;
	if (ref.type == ContentRef::Type::Scene)
		m_studio->removeScene(ref.uid);
	else
		m_studio->removeSource(ref.uid);
}

void MainWindow::renameContent(const ContentRef &ref)
{
	bool ok = false;
	const QString name = QInputDialog::getText(this, QStringLiteral("Renomear"), QStringLiteral("Nome"),
						   QLineEdit::Normal, m_studio->contentName(ref), &ok);
	if (!ok)
		return;
	if (ref.type == ContentRef::Type::Scene)
		m_studio->renameScene(ref.uid, name);
	else
		m_studio->renameSource(ref.uid, name);
}

// ---------------------------------------------------------------------------
// outputs
// ---------------------------------------------------------------------------

void MainWindow::toggleRecording()
{
	if (m_studio->isRecording()) {
		QMessageBox box(QMessageBox::Question, QStringLiteral("Encerrar gravação"),
				QStringLiteral("Encerrar a gravação em andamento?"), QMessageBox::Yes | QMessageBox::Cancel, this);
		box.setDefaultButton(QMessageBox::Cancel);
		box.button(QMessageBox::Yes)->setText(QStringLiteral("Encerrar gravação"));
		if (box.exec() == QMessageBox::Yes)
			m_studio->stopRecording();
		return;
	}
	QString err;
	if (!m_studio->startRecording(&err))
		QMessageBox::warning(this, QStringLiteral("Gravação"), err);
}

void MainWindow::toggleStreaming()
{
	if (m_studio->isStreaming()) {
		QMessageBox box(QMessageBox::Warning, QStringLiteral("Encerrar transmissão"),
				QStringLiteral("Encerrar a transmissão AO VIVO agora?\nO público deixará de receber o sinal."),
				QMessageBox::Yes | QMessageBox::Cancel, this);
		box.setDefaultButton(QMessageBox::Cancel);
		box.button(QMessageBox::Yes)->setText(QStringLiteral("Encerrar transmissão"));
		if (box.exec() == QMessageBox::Yes)
			m_studio->stopStreaming();
		return;
	}
	if (m_studio->profile().stream.server.trimmed().isEmpty() || m_studio->streamKey().isEmpty()) {
		openSettings(SettingsDialog::Streaming);
		if (m_studio->profile().stream.server.trimmed().isEmpty() || m_studio->streamKey().isEmpty())
			return;
	}
	QString err;
	if (!m_studio->startStreaming(&err))
		QMessageBox::warning(this, QStringLiteral("Transmissão"), err);
}

void MainWindow::openSettings(int tab)
{
	SettingsDialog dlg(m_studio, tab, this);
	dlg.exec();
}

void MainWindow::openRecordings()
{
	QString folder = m_studio->profile().recording.folder;
	if (folder.isEmpty())
		folder = OutputManager::defaultRecordingFolder();
	QDir().mkpath(folder);
	QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::switchProfile(const QString &name)
{
	if (m_studio->isRecording() || m_studio->isStreaming()) {
		QMessageBox::information(this, QStringLiteral("Perfil"),
					 QStringLiteral("Encerre a gravação e a transmissão antes de trocar de perfil."));
		m_top->refreshProfiles();
		return;
	}
	saveLayout();
	QString err;
	if (!m_studio->switchProfile(name, &err))
		QMessageBox::warning(this, QStringLiteral("Perfil"), err);
	m_top->refreshProfiles();
}

void MainWindow::newProfile()
{
	bool ok = false;
	const QString name = QInputDialog::getText(this, QStringLiteral("Novo perfil"), QStringLiteral("Nome do perfil"),
						   QLineEdit::Normal, QString(), &ok);
	if (!ok || name.trimmed().isEmpty())
		return;
	const auto copy = QMessageBox::question(this, QStringLiteral("Novo perfil"),
						QStringLiteral("Copiar fontes, cenas, gráficos e configurações do perfil atual?"),
						QMessageBox::Yes | QMessageBox::No);
	saveLayout();
	QString err;
	if (!m_studio->createProfile(name, copy == QMessageBox::Yes, &err))
		QMessageBox::warning(this, QStringLiteral("Novo perfil"), err);
	m_top->refreshProfiles();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	if (m_closing) {
		e->accept();
		return;
	}
	if (m_studio->isRecording() || m_studio->isStreaming()) {
		QMessageBox box(QMessageBox::Warning, QStringLiteral("Sair"),
				QStringLiteral("Há gravação ou transmissão em andamento. Encerrar tudo e sair?"),
				QMessageBox::Yes | QMessageBox::Cancel, this);
		box.setDefaultButton(QMessageBox::Cancel);
		box.button(QMessageBox::Yes)->setText(QStringLiteral("Encerrar e sair"));
		if (box.exec() != QMessageBox::Yes) {
			e->ignore();
			return;
		}
	}
	saveLayout();
	m_closing = true;
	qApp->removeEventFilter(this);
	e->accept();
}

} // namespace ms
