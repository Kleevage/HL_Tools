#include <algorithm>
#include <cassert>
#include <string_view>
#include <utility>
#include <vector>

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QWindow>

#include "version.hpp"

#include "assets/AssetIO.hpp"

#include "filesystem/IFileSystem.hpp"
#include "filesystem/FileSystemConstants.hpp"

#include "qt/QtLogging.hpp"

#include "ui/Credits.hpp"
#include "ui/DragNDropEventFilter.hpp"
#include "ui/EditorContext.hpp"
#include "ui/FileListPanel.hpp"
#include "ui/FullscreenWidget.hpp"
#include "ui/MainWindow.hpp"

#include "ui/assets/Assets.hpp"

#include "ui/options/OptionsDialog.hpp"

#include "ui/settings/GameConfigurationsSettings.hpp"
#include "ui/settings/PathSettings.hpp"
#include "ui/settings/RecentFilesSettings.hpp"

namespace ui
{
constexpr std::string_view TabWidgetAssetProperty{"TabWidgetAssetProperty"};
const QString AssetPathName{QStringLiteral("AssetPath")};

MainWindow::MainWindow(EditorContext* editorContext)
	: QMainWindow()
	, _editorContext(editorContext)
{
	_ui.setupUi(this);

	this->setAttribute(Qt::WidgetAttribute::WA_DeleteOnClose);

	this->setWindowIcon(QIcon{":/hlam.ico"});

	this->installEventFilter(_editorContext->GetDragNDropEventFilter());

	{
		auto undo = _undoGroup->createUndoAction(this);
		auto redo = _undoGroup->createRedoAction(this);

		undo->setShortcut(QKeySequence::StandardKey::Undo);
		redo->setShortcut(QKeySequence::StandardKey::Redo);

		_ui.MenuEdit->addAction(undo);
		_ui.MenuEdit->addAction(redo);
	}

	{
		const auto before = _ui.MenuTools->insertSeparator(_ui.ActionOptions);

		//Create the tool menu for each provider, sort by provider name, then add them all
		std::vector<std::pair<QString, QMenu*>> menus;

		for (auto provider : _editorContext->GetAssetProviderRegistry()->GetAssetProviders())
		{
			if (auto menu = provider->CreateToolMenu(_editorContext); menu)
			{
				menus.emplace_back(provider->GetProviderName(), menu);
			}
		}

		std::sort(menus.begin(), menus.end(), [](const auto& lhs, const auto& rhs)
			{
				return lhs.first.compare(rhs.first, Qt::CaseSensitivity::CaseInsensitive) < 0;
			});

		for (const auto& menu : menus)
		{
			menu.second->setParent(_ui.MenuTools, menu.second->windowFlags());
			_ui.MenuTools->insertMenu(before, menu.second);
		}
	}

	{
		auto fileList = new FileListPanel(_editorContext, this);

		connect(fileList, &FileListPanel::FileSelected, this, &MainWindow::OnFileSelected);

		_fileListDock = new QDockWidget(this);

		_fileListDock->setWidget(fileList);
		_fileListDock->setWindowTitle("File List");

		this->addDockWidget(Qt::DockWidgetArea::LeftDockWidgetArea, _fileListDock);

		_fileListDock->hide();

		_ui.MenuWindows->addAction(_fileListDock->toggleViewAction());
	}

	_assetTabs = new QTabWidget(this);

	//Eliminate the border on the sides so the scene widget takes up all horizontal space
	_assetTabs->setDocumentMode(true);
	_assetTabs->setTabsClosable(true);
	_assetTabs->setElideMode(Qt::TextElideMode::ElideLeft);

	setCentralWidget(_assetTabs);

	setAcceptDrops(true);

	connect(_ui.ActionLoad, &QAction::triggered, this, &MainWindow::OnOpenLoadAssetDialog);
	connect(_ui.ActionSave, &QAction::triggered, this, &MainWindow::OnSaveAsset);
	connect(_ui.ActionSaveAs, &QAction::triggered, this, &MainWindow::OnSaveAssetAs);
	connect(_ui.ActionClose, &QAction::triggered, this, &MainWindow::OnCloseAsset);
	connect(_ui.ActionExit, &QAction::triggered, this, &MainWindow::OnExit);

	connect(_ui.ActionFullscreen, &QAction::triggered, this, &MainWindow::OnGoFullscreen);

	connect(_ui.ActionRefresh, &QAction::triggered, this, &MainWindow::OnRefreshAsset);

	connect(_ui.ActionOptions, &QAction::triggered, this, &MainWindow::OnOpenOptionsDialog);
	connect(_ui.ActionAbout, &QAction::triggered, this, &MainWindow::OnShowAbout);
	connect(_ui.ActionAboutQt, &QAction::triggered, QApplication::instance(), &QApplication::aboutQt);

	connect(_editorContext->GetRecentFiles(), &settings::RecentFilesSettings::RecentFilesChanged, this, &MainWindow::OnRecentFilesChanged);

	connect(_undoGroup, &QUndoGroup::cleanChanged, this, &MainWindow::OnAssetCleanChanged);

	connect(_assetTabs, &QTabWidget::currentChanged, this, &MainWindow::OnAssetTabChanged);
	connect(_assetTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::OnAssetTabCloseRequested);

	connect(_editorContext, &EditorContext::TryingToLoadAsset, this, &MainWindow::TryLoadAsset);
	connect(_editorContext->GetGameConfigurations(), &settings::GameConfigurationsSettings::ActiveConfigurationChanged,
		this, &MainWindow::OnActiveConfigurationChanged);

	_ui.ActionSave->setEnabled(false);
	_ui.ActionSaveAs->setEnabled(false);
	_ui.ActionClose->setEnabled(false);
	_ui.MenuAsset->setEnabled(false);
	_assetTabs->setVisible(false);

	OnRecentFilesChanged();
	OnActiveConfigurationChanged(_editorContext->GetGameConfigurations()->GetActiveConfiguration(), {});

	setWindowTitle({});

	{
		//Construct the file filters used for loading and saving
		auto setupFileFilters = [this](assets::ProviderFeature feature)
		{
			QStringList filters;

			for (auto provider : _editorContext->GetAssetProviderRegistry()->GetAssetProviders())
			{
				if (provider->GetFeatures() & feature)
				{
					auto fileTypes = provider->GetFileTypes();

					for (auto& fileType : fileTypes)
					{
						fileType = QString{"*.%1"}.arg(fileType);
					}

					filters.append(QString{"%1 Files (%2)"}.arg(provider->GetProviderName()).arg(fileTypes.join(' ')));
				}
			}

			QString fileFilters;

			if (!filters.isEmpty())
			{
				fileFilters = filters.join(";;");
			}

			if (!fileFilters.isEmpty())
			{
				fileFilters += ";;";
			}

			fileFilters += "All Files (*.*)";

			return fileFilters;
		};

		_loadFileFilter = setupFileFilters(assets::ProviderFeature::AssetLoading);
		_saveFileFilter = setupFileFilters(assets::ProviderFeature::AssetSaving);
	}

	auto settings = _editorContext->GetSettings();

	{
		settings->beginGroup("main_window");
		const auto screenName = settings->value("screen_name");
		const auto geometry = settings->value("screen_geometry");
		settings->endGroup();

		//Calling this forces the creation of a QWindow handle now, instead of later
		winId();

		//Try to open the window on the screen it was last on
		if (screenName.isValid())
		{
			auto name = screenName.toString();

			for (auto screen : QApplication::screens())
			{
				if (screen->name() == name)
				{
					windowHandle()->setScreen(screen);
					break;
				}
			}
		}

		if (geometry.isValid())
		{
			restoreGeometry(geometry.toByteArray());
		}
	}

	_editorContext->StartTimer();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
	//If the user cancels any close request cancel the window close event as well
	for (int i = 0; i < _assetTabs->count(); ++i)
	{
		const auto asset = GetAsset(i);

		if (!VerifyNoUnsavedChanges(asset))
		{
			event->ignore();
			return;
		}
	}

	//Close each asset
	while (_assetTabs->count() > 0)
	{
		//Don't ask the user to save again
		TryCloseAsset(0, false);
	}

	event->accept();

	auto screen = this->windowHandle()->screen();

	auto name = screen->name();

	auto settings = _editorContext->GetSettings();

	settings->beginGroup("main_window");
	settings->setValue("screen_name", name);
	settings->setValue("screen_geometry", saveGeometry());
	settings->endGroup();

	//Main window cleanup has to be done here because Qt won't call the destructor
	{
		_editorContext->GetTimer()->stop();

		delete _fileListDock;
		_fullscreenWidget.reset();
		_currentAsset.clear();
		delete _assetTabs;
	}
}

assets::Asset* MainWindow::GetAsset(int index) const
{
	return _assetTabs->widget(index)->property(TabWidgetAssetProperty.data()).value<assets::Asset*>();
}

assets::Asset* MainWindow::GetCurrentAsset() const
{
	return _currentAsset;
}

bool MainWindow::SaveAsset(assets::Asset* asset)
{
	assert(asset);

	qCDebug(logging::HLAM) << "Trying to save asset" << asset->GetFileName();

	try
	{
		asset->Save();
	}
	catch (const ::assets::AssetException& e)
	{
		QMessageBox::critical(this, "Error saving asset", QString{"Error saving asset:\n%1"}.arg(e.what()));
		return false;
	}

	auto undoStack = asset->GetUndoStack();

	undoStack->setClean();

	return true;
}

bool MainWindow::VerifyNoUnsavedChanges(assets::Asset* asset)
{
	assert(asset);

	if (asset->GetUndoStack()->isClean())
	{
		return true;
	}

	const QMessageBox::StandardButton action = QMessageBox::question(
		this,
		{},
		QString{"Save changes made to \"%1\"?"}.arg(asset->GetFileName()),
		QMessageBox::StandardButton::Save | QMessageBox::StandardButton::Discard | QMessageBox::StandardButton::Cancel,
		QMessageBox::StandardButton::Save);

	switch (action)
	{
	case QMessageBox::StandardButton::Save: return SaveAsset(asset);
	case QMessageBox::StandardButton::Discard: return true;
	default:
	case QMessageBox::StandardButton::Cancel: return false;
	}
}

bool MainWindow::TryCloseAsset(int index, bool verifyUnsavedChanges)
{
	if (_fullscreenWidget)
	{
		//Always exit the fullscreen window if we're getting a close request
		//The user needs to be able to see the main window and interact with it,
		//and the fullscreen window may be holding a reference to the asset being closed
		_fullscreenWidget->ExitFullscreen();
	}

	{
		const auto asset = GetAsset(index);

		if (verifyUnsavedChanges && !VerifyNoUnsavedChanges(asset))
		{
			//User cancelled or an error occurred
			return false;
		}

		_assetTabs->removeTab(index);

		_undoGroup->removeStack(asset->GetUndoStack());

		delete asset;
	}

	const bool hasOpenAssets = _assetTabs->count() > 0;

	_assetTabs->setVisible(hasOpenAssets);
	_ui.ActionFullscreen->setEnabled(hasOpenAssets);
	_ui.ActionRefresh->setEnabled(hasOpenAssets);

	return true;
}

void MainWindow::UpdateTitle(const QString& fileName, bool hasUnsavedChanges)
{
	setWindowTitle(QString{"%1[*]"}.arg(fileName));
	setWindowModified(hasUnsavedChanges);
}

bool MainWindow::TryLoadAsset(QString fileName)
{
	fileName = QDir::cleanPath(fileName);

	qCDebug(logging::HLAM) << "Trying to load asset" << fileName;

	if (!QFile::exists(fileName))
	{
		qCDebug(logging::HLAM) << "Asset" << fileName << "does not exist";
		QMessageBox::critical(this, "Error loading asset", QString{"Asset \"%1\" does not exist"}.arg(fileName));
		return false;
	}

	try
	{
		auto asset = _editorContext->GetAssetProviderRegistry()->Load(_editorContext, fileName);

		if (nullptr != asset)
		{
			auto currentFileName = asset->GetFileName();

			qCDebug(logging::HLAM) << "Asset" << fileName << "loaded as" << currentFileName;

			connect(asset.get(), &assets::Asset::FileNameChanged, this, &MainWindow::OnAssetFileNameChanged);

			const auto editWidget = asset->GetEditWidget();

			editWidget->setProperty(TabWidgetAssetProperty.data(), QVariant::fromValue(asset.get()));

			_undoGroup->addStack(asset->GetUndoStack());

			//Now owned by this window
			asset->setParent(this);
			asset.release();

			//Use the current filename for this
			const auto index = _assetTabs->addTab(editWidget, currentFileName);

			_assetTabs->setCurrentIndex(index);

			//TODO: this is duplicated between this and TryCloseAsset
			_assetTabs->setVisible(true);
			_ui.ActionFullscreen->setEnabled(true);
			_ui.ActionRefresh->setEnabled(true);

			_editorContext->GetRecentFiles()->Add(fileName);

			qCDebug(logging::HLAM) << "Loaded asset" << fileName;

			return true;
		}
		else
		{
			qCDebug(logging::HLAM) << "Asset" << fileName << "couldn't be loaded";
			QMessageBox::critical(this, "Error loading asset", QString{"Error loading asset \"%1\":\nNull asset returned"}.arg(fileName));
		}
	}
	catch (const ::assets::AssetException& e)
	{
		QMessageBox::critical(this, "Error loading asset", QString{"Error loading asset \"%1\":\n%2"}.arg(fileName).arg(e.what()));
	}

	return false;
}

void MainWindow::OnOpenLoadAssetDialog()
{
	if (const auto fileName = QFileDialog::getOpenFileName(this, "Select asset",
		settings::GetSavedPath(*_editorContext->GetSettings(), AssetPathName),
		_loadFileFilter);
		!fileName.isEmpty())
	{
		settings::SetSavedPath(*_editorContext->GetSettings(), AssetPathName, QFileInfo(fileName).absolutePath());

		TryLoadAsset(fileName);
	}
}

void MainWindow::OnAssetCleanChanged(bool clean)
{
	setWindowModified(!clean);
}

void MainWindow::OnAssetTabChanged(int index)
{
	_ui.MenuAsset->clear();

	bool success = false;

	if (index != -1)
	{
		const auto asset = GetAsset(index);

		if (_currentAsset)
		{
			_currentAsset->SetActive(false);
		}

		_undoGroup->setActiveStack(asset->GetUndoStack());

		UpdateTitle(asset->GetFileName(), !_undoGroup->isClean());
		asset->PopulateAssetMenu(_ui.MenuAsset);

		_currentAsset = asset;

		asset->SetActive(true);

		success = true;
	}

	if (!success)
	{
		_undoGroup->setActiveStack(nullptr);
		setWindowTitle({});
		_currentAsset = nullptr;
	}

	_ui.ActionSave->setEnabled(success);
	_ui.ActionSaveAs->setEnabled(success);
	_ui.ActionClose->setEnabled(success);
	_ui.MenuAsset->setEnabled(success);
}

void MainWindow::OnAssetTabCloseRequested(int index)
{
	TryCloseAsset(index, true);
}

void MainWindow::OnAssetFileNameChanged(const QString& fileName)
{
	auto asset = static_cast<assets::Asset*>(sender());

	const int index = _assetTabs->indexOf(asset->GetEditWidget());

	if (index != -1)
	{
		_assetTabs->setTabText(index, fileName);

		_editorContext->GetRecentFiles()->Add(fileName);

		if (_assetTabs->currentWidget() == asset->GetEditWidget())
		{
			UpdateTitle(asset->GetFileName(), !_undoGroup->isClean());
		}
	}
	else
	{
		QMessageBox::critical(this, "Internal Error", "Asset index not found in assets tab widget");
	}
}

void MainWindow::OnSaveAsset()
{
	SaveAsset(GetCurrentAsset());
}

void MainWindow::OnSaveAssetAs()
{
	const auto asset = GetCurrentAsset();

	QString fileName{QFileDialog::getSaveFileName(this, {}, asset->GetFileName(), _saveFileFilter)};

	if (!fileName.isEmpty())
	{
		//Also update the saved path when saving files
		settings::SetSavedPath(*_editorContext->GetSettings(), AssetPathName, QFileInfo(fileName).absolutePath());
		asset->SetFileName(std::move(fileName));
		SaveAsset(asset);
	}
}

void MainWindow::OnCloseAsset()
{
	if (auto index = this->_assetTabs->currentIndex(); index != -1)
	{
		TryCloseAsset(index, true);
	}
}

void MainWindow::OnRecentFilesChanged()
{
	const auto recentFiles = _editorContext->GetRecentFiles();

	_ui.MenuRecentFiles->clear();

	for (int i = 0; i < recentFiles->GetCount(); ++i)
	{
		_ui.MenuRecentFiles->addAction(recentFiles->At(i), this, &MainWindow::OnOpenRecentFile);
	}

	_ui.MenuRecentFiles->setEnabled(recentFiles->GetCount() > 0);
}

void MainWindow::OnOpenRecentFile()
{
	const auto action = static_cast<QAction*>(sender());

	const QString fileName{action->text()};

	if (!TryLoadAsset(fileName))
	{
		_editorContext->GetRecentFiles()->Remove(fileName);
	}
}

void MainWindow::OnExit()
{
	this->close();
}

void MainWindow::OnGoFullscreen()
{
	if (!_fullscreenWidget)
	{
		//Note: creating this window as a child of the main window causes problems with OpenGL rendering
		//This must be created with no parent to function properly
		_fullscreenWidget = std::make_unique<FullscreenWidget>();
	}

	const auto asset = GetCurrentAsset();

	asset->SetupFullscreenWidget(_fullscreenWidget.get());

	_fullscreenWidget->raise();
	_fullscreenWidget->showFullScreen();
	_fullscreenWidget->activateWindow();
}

void MainWindow::OnFileSelected(const QString& fileName)
{
	TryLoadAsset(fileName);
}

void MainWindow::OnRefreshAsset()
{
	if (auto asset = GetCurrentAsset(); asset)
	{
		if (!VerifyNoUnsavedChanges(asset))
		{
			//User canceled, abort refresh
			return;
		}

		asset->TryRefresh();
	}
}

void MainWindow::OnOpenOptionsDialog()
{
	options::OptionsDialog dialog{_editorContext, this};

	dialog.exec();
}

void MainWindow::OnShowAbout()
{
	const QString programName{QApplication::applicationName()};

	QString buildConfiguration;

#ifdef NDEBUG
	buildConfiguration = "Release";
#else
	buildConfiguration = "Debug";
#endif

	QMessageBox::about(this, "About " + programName,
		QString::fromUtf8(
			u8"%1 %2.%3.%4\n\n"
			u8"2021 Sam Vanheer\n\n"
			u8"Email:    sam.vanheer@outlook.com\n\n"
			u8"Build Configuration: %5\n"
			u8"Git Info:\n"
			u8"\tBranch: %6\n"
			u8"\tTag: %7\n"
			u8"\tCommit Hash: %8\n\n"
			u8"Based on Jed's Half-Life Model Viewer v1.3 � 2004 Neil \'Jed\' Jedrzejewski\n"
			u8"Email:    jed@wunderboy.org\n"
			u8"Web:      http://www.wunderboy.org/\n\n"
			u8"Also based on Half-Life Model Viewer v1.25 � 2002 Mete Ciragan\n"
			u8"Email:    mete@swissquake.ch\n"
			u8"Web:      http://www.milkshape3d.com/\n\n")
			.arg(programName)
			.arg(HLAMVersionMajor)
			.arg(HLAMVersionMinor)
			.arg(HLAMVersionPatch)
			.arg(buildConfiguration)
			.arg(QString::fromUtf8(HLAMGitBranchName.data()))
			.arg(QString::fromUtf8(HLAMGitTagName.data()))
			.arg(QString::fromUtf8(HLAMGitCommitHash.data()))
		+ QString::fromUtf8(GetSharedCredits().c_str())
	);
}

void MainWindow::SetupFileSystem(std::pair<settings::GameEnvironment*, settings::GameConfiguration*> activeConfiguration)
{
	auto fileSystem = _editorContext->GetFileSystem();

	fileSystem->RemoveAllSearchPaths();

	const auto environment = activeConfiguration.first;
	const auto configuration = activeConfiguration.second;
	const auto defaultGameConfiguration = environment->GetGameConfigurationById(environment->GetDefaultModId());

	fileSystem->SetBasePath(environment->GetInstallationPath().toStdString().c_str());

	const auto directoryExtensions{filesystem::GetSteamPipeDirectoryExtensions()};

	const auto gameDir{defaultGameConfiguration->GetDirectory().toStdString()};
	const auto modDir{configuration->GetDirectory().toStdString()};

	//Add mod dirs first since they override game dirs
	if (gameDir != modDir)
	{
		for (const auto& extension : directoryExtensions)
		{
			fileSystem->AddSearchPath((modDir + extension).c_str());
		}
	}

	for (const auto& extension : directoryExtensions)
	{
		fileSystem->AddSearchPath((gameDir + extension).c_str());
	}
}

void MainWindow::OnActiveConfigurationChanged(std::pair<settings::GameEnvironment*, settings::GameConfiguration*> current,
	std::pair<settings::GameEnvironment*, settings::GameConfiguration*> previous)
{
	if (previous.second)
	{
		disconnect(previous.second, &settings::GameConfiguration::DirectoryChanged, this, &MainWindow::OnGameConfigurationDirectoryChanged);
	}

	if (current.second)
	{
		connect(current.second, &settings::GameConfiguration::DirectoryChanged, this, &MainWindow::OnGameConfigurationDirectoryChanged);

		SetupFileSystem(current);
	}
	else
	{
		auto fileSystem = _editorContext->GetFileSystem();

		fileSystem->RemoveAllSearchPaths();
	}
}

void MainWindow::OnGameConfigurationDirectoryChanged()
{
	SetupFileSystem(_editorContext->GetGameConfigurations()->GetActiveConfiguration());
}
}
