#pragma once

#include <memory>

#include <QTabWidget>
#include <QWidget>

#include "ui/IInputSink.hpp"
#include "ui/assets/studiomodel/StudioModelAsset.hpp"

namespace graphics
{
class Scene;
}

namespace ui
{
class EditorContext;
class SceneWidget;

namespace camera_operators
{
class CameraOperator;
}

namespace settings
{
class StudioModelSettings;
}

namespace assets::studiomodel
{
class StudioModelAsset;
class Timeline;

class StudioModelEditWidget final : public QWidget, public IInputSink
{
	Q_OBJECT

public:
	StudioModelEditWidget(EditorContext* editorContext, settings::StudioModelSettings* studioModelSettings, StudioModelAsset* asset, QWidget* parent = nullptr);
	~StudioModelEditWidget();

	StudioModelAsset* GetAsset() const { return _asset; }

	void OnMouseEvent(QMouseEvent* event) override;

signals:
	void DockPanelChanged(QWidget* current, QWidget* previous);

private slots:
	void OnTick();

	void OnSceneWidgetMouseEvent(QMouseEvent* event);

	void OnFloorLengthChanged(int length);

	void OnTabChanged(int index);

private:
	StudioModelAsset* const _asset;

	SceneWidget* _sceneWidget;

	QWidget* _controlAreaWidget;

	QTabWidget* _dockPanels;

	QWidget* _currentTab{};

	Timeline* _timeline;

	//TODO: temporary; will need to be set up somewhere else eventually
	std::unique_ptr<camera_operators::CameraOperator> _cameraOperator;
};
}
}
