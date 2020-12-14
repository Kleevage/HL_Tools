#include <stdexcept>

#include "graphics/Scene.hpp"

#include "ui/EditorContext.hpp"
#include "ui/FullscreenWidget.hpp"
#include "ui/SceneWidget.hpp"
#include "ui/assets/studiomodel/StudioModelAsset.hpp"
#include "ui/assets/studiomodel/StudioModelEditWidget.hpp"

namespace ui::assets::studiomodel
{
StudioModelAsset::StudioModelAsset(QString&& fileName,
	EditorContext* editorContext, const StudioModelAssetProvider* provider, std::unique_ptr<studiomdl::CStudioModel>&& studioModel)
	: Asset(std::move(fileName))
	, _editorContext(editorContext)
	, _provider(provider)
	, _studioModel(std::move(studioModel))
	, _scene(std::make_unique<graphics::Scene>(editorContext->GetSoundSystem()))
{
}

StudioModelAsset::~StudioModelAsset() = default;

QWidget* StudioModelAsset::CreateEditWidget(EditorContext* editorContext)
{
	return new StudioModelEditWidget(editorContext, _provider->GetSettings(), this);
}

void StudioModelAsset::SetupFullscreenWidget(EditorContext* editorContext, FullscreenWidget* fullscreenWidget)
{
	const auto sceneWidget = new SceneWidget(GetScene(), fullscreenWidget);

	fullscreenWidget->setCentralWidget(sceneWidget->GetContainer());

	sceneWidget->connect(editorContext, &EditorContext::Tick, sceneWidget, &SceneWidget::requestUpdate);

	//Filter key events on the scene widget so we can capture exit even if it has focus
	sceneWidget->installEventFilter(fullscreenWidget);
}

void StudioModelAsset::Save(const QString& fileName)
{
	_provider->Save(fileName, *this);
}

bool StudioModelAssetProvider::CanLoad(const QString& fileName) const
{
	//TODO:
	return true;
}

std::unique_ptr<Asset> StudioModelAssetProvider::Load(EditorContext* editorContext, const QString& fileName) const
{
	//TODO: this throws specific exceptions. They need to be generalized so the caller can handle them
	auto studioModel = studiomdl::LoadStudioModel(fileName.toStdString().c_str());

	return std::make_unique<StudioModelAsset>(QString{fileName}, editorContext, this, std::move(studioModel));
}

void StudioModelAssetProvider::Save(const QString& fileName, Asset& asset) const
{
	//TODO:
	if (asset.GetAssetType() == GetAssetType())
	{
		Save(fileName, static_cast<StudioModelAsset&>(asset));
	}
	else
	{
		//TODO: maybe allow conversion from other asset types to this one, otherwise remove this method from the provider API
		throw std::runtime_error("Cannot convert asset type to studiomodel");
	}
}

void StudioModelAssetProvider::Save(const QString& fileName, StudioModelAsset& asset) const
{
	//TODO:
}
}
