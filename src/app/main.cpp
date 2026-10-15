// Marshalls Studio entry point.
#include "Log.h"
#include "MainWindow.h"
#include "ObsPlatform.h"
#include "Studio.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QLockFile>
#include <QTranslator>
#include <QMessageBox>
#include <QSocketNotifier>
#include <QStandardPaths>

#if !defined(_WIN32)
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace ms;

namespace {

void qtMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
	LogLevel l = LogLevel::Info;
	switch (type) {
	case QtDebugMsg: l = LogLevel::Debug; break;
	case QtInfoMsg: l = LogLevel::Info; break;
	case QtWarningMsg: l = LogLevel::Warning; break;
	case QtCriticalMsg:
	case QtFatalMsg: l = LogLevel::Error; break;
	}
	Log::instance().write(l, QStringLiteral("qt"), msg);
}

#if !defined(_WIN32)
// SIGTERM/SIGINT (system shutdown, Ctrl+C) end the session through the normal
// path so outputs are finalized and the configuration is saved.
int g_signalFd[2] = {-1, -1};
void unixSignalHandler(int)
{
	const char c = 1;
	ssize_t r = ::write(g_signalFd[0], &c, 1);
	(void)r;
}
#endif

} // namespace

int main(int argc, char *argv[])
{
#if !defined(_WIN32) && !defined(__APPLE__)
	// libobs draws into native X11 windows; on Wayland sessions run through XWayland.
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		qputenv("QT_QPA_PLATFORM", "xcb");
#endif
	QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
	QApplication app(argc, argv);
	QApplication::setOrganizationName(QStringLiteral("Marshalls"));
	QApplication::setApplicationName(QStringLiteral("Marshalls Studio"));
	QApplication::setApplicationVersion(QStringLiteral(MS_VERSION));
	{
		QIcon icon;
		for (int s : {32, 48, 64, 128, 256})
			icon.addFile(QStringLiteral(":/marshalls/marshalls-%1.png").arg(s), QSize(s, s));
		QApplication::setWindowIcon(icon);
	}

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Marshalls Studio — produção audiovisual ao vivo"));
	parser.addHelpOption();
	parser.addVersionOption();
	QCommandLineOption profileOpt(QStringLiteral("profile"), QStringLiteral("Perfil a abrir."), QStringLiteral("nome"));
	QCommandLineOption portableOpt(QStringLiteral("portable"), QStringLiteral("Guarda configuração ao lado do executável."));
	QCommandLineOption verboseOpt(QStringLiteral("verbose"), QStringLiteral("Log detalhado (debug)."));
	parser.addOption(profileOpt);
	parser.addOption(portableOpt);
	parser.addOption(verboseOpt);
	parser.process(app);

	Studio::Paths paths = Studio::defaultPaths();
	if (parser.isSet(portableOpt)) {
		const QString base = QCoreApplication::applicationDirPath() + QStringLiteral("/config");
		paths = {base, base + "/logs", base + "/plugin_config", base + "/secrets"};
	}
	QDir().mkpath(paths.configRoot);

	Log::instance().open(paths.logDir);
	Log::instance().setMinimumLevel(parser.isSet(verboseOpt) ? LogLevel::Debug : LogLevel::Info);
	qInstallMessageHandler(qtMessageHandler);
	logInfo("app", QStringLiteral("Marshalls Studio %1 iniciando (%2)").arg(MS_VERSION, QSysInfo::prettyProductName()));

	theme::apply(app);

	// Standard Qt dialogs/buttons in Brazilian Portuguese.
	QTranslator qtTranslator;
	for (const QString &dir : {QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
				   QLibraryInfo::path(QLibraryInfo::TranslationsPath)})
		if (qtTranslator.load(QLocale(QStringLiteral("pt_BR")), QStringLiteral("qtbase"), QStringLiteral("_"), dir))
			break;
	app.installTranslator(&qtTranslator);

	// One instance per configuration: two instances would fight for devices.
	QLockFile instanceLock(paths.configRoot + QStringLiteral("/instance.lock"));
	instanceLock.setStaleLockTime(0);
	if (!instanceLock.tryLock(200)) {
		QMessageBox::warning(nullptr, QStringLiteral("Marshalls Studio"),
				     QStringLiteral("O Marshalls Studio já está em execução."));
		return 1;
	}

	// Unclean shutdown detection.
	QStringList notes;
	const QString sentinel = paths.configRoot + QStringLiteral("/session.running");
	if (QFile::exists(sentinel)) {
		notes << QStringLiteral("A sessão anterior terminou de forma inesperada. A configuração foi preservada; "
					"gravações MKV interrompidas permanecem reproduzíveis até o último trecho gravado.");
		logWarn("app", "Sessão anterior não foi encerrada corretamente");
	}
	{
		QFile f(sentinel);
		if (f.open(QIODevice::WriteOnly))
			f.write(QByteArray::number(QCoreApplication::applicationPid()));
	}

	Studio studio(paths);
	EngineInit init;
	init.moduleConfigDir = paths.moduleConfig;
	init.x11Display = ObsPlatform::qtX11Display();
	init.wayland = ObsPlatform::qtIsWayland();
	QString err;
	if (!studio.start(parser.value(profileOpt), init, &err)) {
		logError("app", err);
		QMessageBox::critical(nullptr, QStringLiteral("Marshalls Studio"),
				      QStringLiteral("Não foi possível iniciar o motor de mídia.\n\n%1\n\nLog: %2")
					      .arg(err, Log::instance().currentPath()));
		QFile::remove(sentinel);
		return 2;
	}
	if (!studio.startupNotes().isEmpty())
		notes << studio.startupNotes();

#if !defined(_WIN32)
	if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFd) == 0) {
		auto *notifier = new QSocketNotifier(g_signalFd[1], QSocketNotifier::Read, &app);
		QObject::connect(notifier, &QSocketNotifier::activated, &app, [] {
			char c;
			ssize_t r = ::read(g_signalFd[1], &c, 1);
			(void)r;
			logWarn("app", "Sinal de término recebido; encerrando com segurança");
			QApplication::closeAllWindows();
			QApplication::quit();
		});
		struct sigaction sa = {};
		sa.sa_handler = unixSignalHandler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART;
		sigaction(SIGTERM, &sa, nullptr);
		sigaction(SIGINT, &sa, nullptr);
	}
#endif

	int rc = 0;
	{
		MainWindow w(&studio);
		w.show();
		w.showStartupNotes(notes);
		rc = app.exec();
	} // displays are destroyed before the engine shuts down
	studio.stop();
	QFile::remove(sentinel);
	logInfo("app", QStringLiteral("Encerrado normalmente"));
	Log::instance().close();
	return rc;
}
