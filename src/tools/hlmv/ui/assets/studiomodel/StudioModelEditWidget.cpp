#include <QBoxLayout>

#include "entity/CHLMVStudioModelEntity.h"

#include "game/entity/CBaseEntity.h"
#include "game/entity/CBaseEntityList.h"
#include "game/entity/CEntityManager.h"
#include "graphics/Scene.hpp"

#include "ui/EditorContext.hpp"
#include "ui/SceneWidget.hpp"
#include "ui/assets/studiomodel/StudioModelAsset.hpp"
#include "ui/assets/studiomodel/StudioModelEditWidget.hpp"

#include "ui/assets/studiomodel/dockpanels/StudioModelAttachmentsPanel.hpp"
#include "ui/assets/studiomodel/dockpanels/StudioModelBodyPartsPanel.hpp"
#include "ui/assets/studiomodel/dockpanels/StudioModelBonesPanel.hpp"
#include "ui/assets/studiomodel/dockpanels/StudioModelDisplayPanel.hpp"
#include "ui/assets/studiomodel/dockpanels/StudioModelGlobalFlagsPanel.hpp"
#include "ui/assets/studiomodel/dockpanels/StudioModelHitboxesPanel.hpp"
#include "ui/assets/studiomodel/dockpanels/StudioModelModelDataPanel.hpp"
#include "ui/assets/studiomodel/dockpanels/StudioModelSequencesPanel.hpp"
#include "ui/assets/studiomodel/dockpanels/StudioModelTexturesPanel.hpp"
#include "ui/assets/studiomodel/dockpanels/Timeline.hpp"

#include "ui/camera_operators/ArcBallCameraOperator.hpp"
#include "ui/camera_operators/CameraOperator.hpp"

namespace ui::assets::studiomodel
{
StudioModelEditWidget::StudioModelEditWidget(EditorContext* editorContext, StudioModelAsset* asset, QWidget* parent)
	: QWidget(parent)
	, _asset(asset)
{
	auto scene = std::make_unique<graphics::Scene>(editorContext->GetSoundSystem());

	_context = new StudioModelContext(editorContext, asset, scene.get(), this);

	_context->PushInputSink(this);

	scene->FloorLength = editorContext->GetFloorLength();

	auto entity = static_cast<CHLMVStudioModelEntity*>(scene->GetEntityContext()->EntityManager->Create("studiomodel", scene->GetEntityContext(),
		glm::vec3(), glm::vec3(), false));

	if (nullptr != entity)
	{
		entity->SetModel(_asset->GetStudioModel());

		entity->Spawn();

		scene->SetEntity(entity);
	}

	_cameraOperator = std::make_unique<camera_operators::ArcBallCameraOperator>();

	//TODO: scene ownership needs to be reworked
	_sceneWidget = new SceneWidget(std::move(scene), this);

	_controlAreaWidget = new QWidget(this);

	_dockPanels = new QTabWidget(_controlAreaWidget);

	_dockPanels->setStyleSheet("QTabWidget::pane { padding: 0px; }");

	auto texturesPanel = new StudioModelTexturesPanel(_context);
	auto bonesPanel = new StudioModelBonesPanel(_context);
	auto attachmentsPanel = new StudioModelAttachmentsPanel(_context);
	auto hitboxesPanel = new StudioModelHitboxesPanel(_context);

	_dockPanels->addTab(new StudioModelDisplayPanel(_context), "Model Display");
	_dockPanels->addTab(new StudioModelSequencesPanel(_context), "Sequences");
	_dockPanels->addTab(new StudioModelBodyPartsPanel(_context), "Body Parts");
	_dockPanels->addTab(texturesPanel, "Textures");
	_dockPanels->addTab(new StudioModelModelDataPanel(_context), "Model Data");
	_dockPanels->addTab(new StudioModelGlobalFlagsPanel(_context), "Global Flags");
	_dockPanels->addTab(bonesPanel, "Bones");
	_dockPanels->addTab(attachmentsPanel, "Attachments");
	_dockPanels->addTab(hitboxesPanel, "Hitboxes");

	_timeline = new Timeline(_context, _controlAreaWidget);

	auto layout = new QVBoxLayout(this);

	layout->setContentsMargins(0, 0, 0, 0);

	layout->addWidget(_sceneWidget->GetContainer(), 1);

	layout->addWidget(_controlAreaWidget);

	setLayout(layout);

	{
		auto controlAreaLayout = new QVBoxLayout(_controlAreaWidget);

		controlAreaLayout->addWidget(_dockPanels);
		controlAreaLayout->addWidget(_timeline);

		_controlAreaWidget->setLayout(controlAreaLayout);
	}

	//TODO: need to initialize the background color to its default value here, as specified in the options dialog
	_context->SetBackgroundColor({63, 127, 127});

	//Listen to the main timer to update as needed
	connect(editorContext, &EditorContext::Tick, this, &StudioModelEditWidget::OnTick);
	connect(_sceneWidget, &SceneWidget::MouseEvent, this, &StudioModelEditWidget::OnSceneWidgetMouseEvent);
	connect(editorContext, &EditorContext::FloorLengthChanged, this, &StudioModelEditWidget::OnFloorLengthChanged);

	connect(_dockPanels, &QTabWidget::currentChanged, this, &StudioModelEditWidget::OnTabChanged);

	connect(_sceneWidget, &SceneWidget::CreateDeviceResources, texturesPanel, &StudioModelTexturesPanel::OnCreateDeviceResources);
	connect(this, &StudioModelEditWidget::DockPanelChanged, texturesPanel, &StudioModelTexturesPanel::OnDockPanelChanged);

	connect(this, &StudioModelEditWidget::DockPanelChanged, bonesPanel, &StudioModelBonesPanel::OnDockPanelChanged);
	connect(this, &StudioModelEditWidget::DockPanelChanged, attachmentsPanel, &StudioModelAttachmentsPanel::OnDockPanelChanged);
	connect(this, &StudioModelEditWidget::DockPanelChanged, hitboxesPanel, &StudioModelHitboxesPanel::OnDockPanelChanged);
}

StudioModelEditWidget::~StudioModelEditWidget() = default;

void StudioModelEditWidget::OnMouseEvent(QMouseEvent* event)
{
	_cameraOperator->MouseEvent(*_sceneWidget->GetScene()->GetCamera(), *event);
}

void StudioModelEditWidget::OnTick()
{
	_sceneWidget->GetScene()->Tick();

	_sceneWidget->requestUpdate();

	emit _context->Tick();
}

void StudioModelEditWidget::OnSceneWidgetMouseEvent(QMouseEvent* event)
{
	if (auto inputSink = _context->GetInputSink(); inputSink)
	{
		inputSink->OnMouseEvent(event);
	}
}

void StudioModelEditWidget::OnFloorLengthChanged(int length)
{
	_sceneWidget->GetScene()->FloorLength = length;
}

void StudioModelEditWidget::OnTabChanged(int index)
{
	auto previous = _currentTab;
	_currentTab = _dockPanels->currentWidget();

	emit DockPanelChanged(_currentTab, previous);
}
}
