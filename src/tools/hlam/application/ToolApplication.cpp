#include <cstdlib>
#include <memory>
#include <stdexcept>

#include <GL/glew.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QScopedPointer>
#include <QSettings>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QTextStream>

#include "application/ToolApplication.hpp"

#include "qt/QtLogging.hpp"

#include "ui/EditorContext.hpp"
#include "ui/MainWindow.hpp"

#include "ui/assets/Assets.hpp"
#include "ui/assets/studiomodel/StudioModelAsset.hpp"
#include "ui/assets/studiomodel/StudioModelColors.hpp"

#include "ui/options/OptionsPageColors.hpp"
#include "ui/options/OptionsPageGameConfigurations.hpp"
#include "ui/options/OptionsPageGeneral.hpp"
#include "ui/options/OptionsPageRegistry.hpp"
#include "ui/options/OptionsPageStudioModel.hpp"
#include "ui/options/OptionsPageStyle.hpp"

#include "ui/settings/ColorSettings.hpp"
#include "ui/settings/GameConfigurationsSettings.hpp"
#include "ui/settings/GeneralSettings.hpp"
#include "ui/settings/RecentFilesSettings.hpp"
#include "ui/settings/StudioModelSettings.hpp"
#include "ui/settings/StyleSettings.hpp"

using namespace ui::assets;

const QString LogBaseFileName{QStringLiteral("HLAM-Log.txt")};

QString LogFileName = LogBaseFileName;

const QtMessageHandler DefaultMessageHandler = qInstallMessageHandler(nullptr);

void FileMessageOutput(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
	QByteArray localMsg = msg.toLocal8Bit();

	QFile logFile{LogFileName};

	if (!logFile.open(QFile::WriteOnly | QFile::Append))
	{
		QMessageBox::critical(nullptr, "Error", QString{"Couldn't open file \"%1\" for writing log messages"}
			.arg(QFileInfo{logFile}.absoluteFilePath()));
		return;
	}

	QTextStream stream{&logFile};

	const char* messageType = "Unknown";

	switch (type)
	{
	case QtDebugMsg:
		messageType = "Debug";
		break;

	case QtInfoMsg:
		messageType = "Info";
		break;

	case QtWarningMsg:
		messageType = "Warning";
		break;

	case QtCriticalMsg:
		messageType = "Critical";
		break;

	case QtFatalMsg:
		messageType = "Fatal";
		break;
	}

	stream << messageType << ": " << msg << " (" << context.file << ":" << context.line << ", " << context.function << ")\n";

	//Let the default handler handle abort
	/*
	if (type == QtFatalMsg)
	{
		abort();
	}
	*/

	DefaultMessageHandler(type, context, msg);
}

ToolApplication::ToolApplication() = default;
ToolApplication::~ToolApplication() = default;

int ToolApplication::Run(int argc, char* argv[])
{
	try
	{
		const QString programName{"Half-Life Asset Manager"};

		ConfigureApplication(programName);

		ConfigureOpenGL();

		QApplication app(argc, argv);

		_application = &app;

		connect(&app, &QApplication::aboutToQuit, this, &ToolApplication::OnExit);

		const auto [isPortable, logDebugMsgsToFile, fileName] = ParseCommandLine(app);

		LogFileName = QApplication::applicationDirPath() + QDir::separator() + LogBaseFileName;

		QFile::remove(LogFileName);

		if (logDebugMsgsToFile)
		{
			qInstallMessageHandler(&FileMessageOutput);
		}

		auto settings{CreateSettings(programName, isPortable)};

		{
			const auto openGLFormat = QOpenGLContext::globalShareContext()->format();

			auto makeVersionCode = [](int major, int minor)
			{
				return (major << 8) + minor;
			};

			const int versionCode = makeVersionCode(openGLFormat.majorVersion(), openGLFormat.minorVersion());

			const int minimumSupportedVersionCode = makeVersionCode(2, 1);

			//Only check this once
			if (!settings->value("graphics/checked_opengl_version", false).toBool() && versionCode < minimumSupportedVersionCode)
			{
				QMessageBox::warning(nullptr, "Warning", QString{"%1 may not work correctly with your version of OpenGL (%2.%3)"}
					.arg(programName).arg(openGLFormat.majorVersion()).arg(openGLFormat.minorVersion()));

				settings->setValue("graphics/checked_opengl_version", true);
			}
		}

		if (CheckSingleInstance(programName, fileName, *settings))
		{
			return EXIT_SUCCESS;
		}

		_editorContext = CreateEditorContext(std::move(settings));

		const auto offscreen{InitializeOpenGL()};

		if (!offscreen.first ||!offscreen.second)
		{
			return EXIT_FAILURE;
		}

		_editorContext->SetOffscreenContext(offscreen.first);
		_editorContext->SetOffscreenSurface(offscreen.second);

		_mainWindow = new ui::MainWindow(_editorContext.get());

		if (!fileName.isEmpty())
		{
			_editorContext->TryLoadAsset(fileName);
		}

		//Note: must come after the file is loaded or it won't actually show maximized
		_mainWindow->showMaximized();

		return app.exec();
	}
	catch (const std::exception& e)
	{
		qDebug() << "Unhandled exception:" << e.what();
		throw;
	}
}

void ToolApplication::ConfigureApplication(const QString& programName)
{
	QApplication::setOrganizationName(programName);
	QApplication::setOrganizationDomain("https://github.com/Solokiller/HL_Tools");
	QApplication::setApplicationName(programName);
	QApplication::setApplicationDisplayName(programName);

	QSettings::setDefaultFormat(QSettings::Format::IniFormat);
}

void ToolApplication::ConfigureOpenGL()
{
	//Neither OpenGL ES nor Software OpenGL will work here
	QApplication::setAttribute(Qt::ApplicationAttribute::AA_UseDesktopOpenGL, true);
	QApplication::setAttribute(Qt::ApplicationAttribute::AA_ShareOpenGLContexts, true);

	//Set up the OpenGL surface settings to match the Half-Life engine's requirements
	//Vanilla Half-Life uses OpenGL 1.0 for game rendering
	//TODO: eventually an option should be added to allow switching to 3.3 for shader based rendering
	QSurfaceFormat::FormatOptions formatOptions{QSurfaceFormat::FormatOption::DeprecatedFunctions};

#ifdef _DEBUG
	formatOptions.setFlag(QSurfaceFormat::FormatOption::DebugContext, true);
#endif

	QSurfaceFormat defaultFormat{formatOptions};

	defaultFormat.setMajorVersion(1);
	defaultFormat.setMinorVersion(0);
	defaultFormat.setProfile(QSurfaceFormat::OpenGLContextProfile::CompatibilityProfile);

	defaultFormat.setDepthBufferSize(24);
	defaultFormat.setStencilBufferSize(8);
	defaultFormat.setSwapBehavior(QSurfaceFormat::SwapBehavior::DoubleBuffer);
	defaultFormat.setRedBufferSize(4);
	defaultFormat.setGreenBufferSize(4);
	defaultFormat.setBlueBufferSize(4);
	defaultFormat.setAlphaBufferSize(0);

	qCDebug(logging::HLAM) << "Configuring OpenGL for" << defaultFormat;

	QSurfaceFormat::setDefaultFormat(defaultFormat);
}

std::tuple<bool, bool, QString> ToolApplication::ParseCommandLine(QApplication& application)
{
	QCommandLineParser parser;

	parser.addOption(QCommandLineOption{"portable", "Launch in portable mode"});

	parser.addOption(QCommandLineOption{"log-to-file", "Log debug messages to a file"});

	parser.addPositionalArgument("fileName", "Filename of the model to load on startup", "[fileName]");

	parser.process(application);

	const bool isPortable = parser.isSet("portable");

	const bool logDebugMsgsToFile = parser.isSet("log-to-file");

	const auto positionalArguments = parser.positionalArguments();

	QString fileName;

	if (!positionalArguments.empty())
	{
		fileName = positionalArguments[0];
	}

	return std::make_tuple(isPortable, logDebugMsgsToFile, fileName);
}

std::unique_ptr<QSettings> ToolApplication::CreateSettings(const QString& programName, bool isPortable)
{
	if (isPortable)
	{
		return std::make_unique<QSettings>(QString{"%1.ini"}.arg(programName), QSettings::Format::IniFormat);
	}
	else
	{
		return std::make_unique<QSettings>();
	}
}

bool ToolApplication::CheckSingleInstance(const QString& programName, const QString& fileName, QSettings& settings)
{
	if (ui::settings::GeneralSettings::ShouldUseSingleInstance(settings))
	{
		_singleInstance.reset(new SingleInstance());

		if (!_singleInstance->Create(programName, fileName))
		{
			return true;
		}

		connect(_singleInstance.get(), &SingleInstance::FileNameReceived, this, &ToolApplication::OnFileNameReceived);
	}

	return false;
}

std::unique_ptr<ui::EditorContext> ToolApplication::CreateEditorContext(std::unique_ptr<QSettings>&& settings)
{
	const auto colorSettings{std::make_shared<ui::settings::ColorSettings>()};
	const auto generalSettings{std::make_shared<ui::settings::GeneralSettings>()};
	const auto gameConfigurationsSettings{std::make_shared<ui::settings::GameConfigurationsSettings>()};
	const auto recentFilesSettings{std::make_shared<ui::settings::RecentFilesSettings>()};
	const auto studioModelSettings{std::make_shared<ui::settings::StudioModelSettings>()};
	const auto styleSettings{std::make_shared<ui::settings::StyleSettings>()};

	connect(styleSettings.get(), &ui::settings::StyleSettings::StylePathChanged, this, &ToolApplication::OnStylePathChanged);

	//TODO: this needs to be simplified and moved somewhere else
	const auto addColor = [&](const studiomodel::ColorInfo& color)
	{
		colorSettings->Add(color.Name, color.DefaultColor);
	};

	addColor(studiomodel::GroundColor);
	addColor(studiomodel::BackgroundColor);
	addColor(studiomodel::CrosshairColor);
	addColor(studiomodel::LightColor);
	addColor(studiomodel::WireframeColor);

	//TODO: settings loading needs to be made more flexible
	colorSettings->LoadSettings(*settings);
	generalSettings->LoadSettings(*settings);
	recentFilesSettings->LoadSettings(*settings);
	gameConfigurationsSettings->LoadSettings(*settings);
	studioModelSettings->LoadSettings(*settings);
	styleSettings->LoadSettings(*settings);

	auto optionsPageRegistry{std::make_unique<ui::options::OptionsPageRegistry>()};

	optionsPageRegistry->AddPage(std::make_unique<ui::options::OptionsPageGeneral>(generalSettings, recentFilesSettings));
	optionsPageRegistry->AddPage(std::make_unique<ui::options::OptionsPageColors>(colorSettings));
	optionsPageRegistry->AddPage(std::make_unique<ui::options::OptionsPageGameConfigurations>(gameConfigurationsSettings));
	optionsPageRegistry->AddPage(std::make_unique<ui::options::OptionsPageStudioModel>(studioModelSettings));
	optionsPageRegistry->AddPage(std::make_unique<ui::options::OptionsPageStyle>(styleSettings));

	auto assetProviderRegistry{std::make_unique<ui::assets::AssetProviderRegistry>()};

	auto studioModelAssetProvider = std::make_unique<studiomodel::StudioModelAssetProvider>(studioModelSettings);
	auto studioModelImportProvider = std::make_unique<studiomodel::StudioModelDolImportProvider>(studioModelAssetProvider.get());

	assetProviderRegistry->AddProvider(std::move(studioModelAssetProvider));
	assetProviderRegistry->AddProvider(std::move(studioModelImportProvider));

	return std::make_unique<ui::EditorContext>(
		settings.release(),
		generalSettings,
		colorSettings,
		recentFilesSettings,
		gameConfigurationsSettings,
		std::move(optionsPageRegistry),
		std::move(assetProviderRegistry));
}

std::pair<QOpenGLContext*, QOffscreenSurface*> ToolApplication::InitializeOpenGL()
{
	auto context{std::make_unique<QOpenGLContext>()};

	context->setFormat(QSurfaceFormat::defaultFormat());

	const auto shareContext{QOpenGLContext::globalShareContext()};

	context->setShareContext(shareContext);
	context->setScreen(shareContext->screen());

	if (!context->create())
	{
		QMessageBox::critical(nullptr, "Fatal Error", "Couldn't create OpenGL context");
		return {};
	}

	auto surface{std::make_unique<QOffscreenSurface>(context->screen(), this)};

	surface->setFormat(context->format());
	surface->setScreen(context->screen());
	surface->create();

	if (!context->makeCurrent(surface.get()))
	{
		QMessageBox::critical(nullptr, "Fatal Error", "Couldn't make offscreen surface context current");
		return {};
	}

	const std::unique_ptr<QOpenGLContext, void (*)(QOpenGLContext*)> cleanup{context.get(), [](QOpenGLContext* ctx)
		{
			return ctx->doneCurrent();
		}};

	glewExperimental = GL_TRUE;

	GLenum error = glewInit();

	if (GLEW_OK != error)
	{
		QMessageBox::critical(nullptr, "Fatal Error", QString{"Error initializing GLEW:\n%1"}.arg(reinterpret_cast<const char*>(glewGetErrorString(error))));
		return {};
	}

	return {context.release(), surface.release()};
}

void ToolApplication::OnExit()
{
	const auto settings = _editorContext->GetSettings();

	_editorContext->GetRecentFiles()->SaveSettings(*settings);

	settings->sync();

	if (_singleInstance)
	{
		_singleInstance.reset();
	}

	_editorContext.reset();
}

void ToolApplication::OnFileNameReceived(const QString& fileName)
{
	if (_mainWindow->isMaximized())
	{
		_mainWindow->showMaximized();
	}
	else
	{
		_mainWindow->showNormal();
	}

	_mainWindow->activateWindow();

	_editorContext->TryLoadAsset(fileName);
}

void ToolApplication::OnStylePathChanged(const QString& stylePath)
{
	auto file = std::make_unique<QFile>(stylePath);
	file->open(QFile::ReadOnly | QFile::Text);

	if (file->isOpen())
	{
		auto stream = std::make_unique<QTextStream>(file.get());

		_application->setStyleSheet(stream->readAll());
	}
	else
	{
		_application->setStyleSheet({});
	}
}
