#ifndef CONTROLPANELS_CBASECONTROLPANEL_H
#define CONTROLPANELS_CBASECONTROLPANEL_H

#include "../wxHLMV.h"

#include <wx/sizer.h>

class StudioModel;

namespace hlmv
{
class CHLMV;

class CBaseControlPanel : public wxPanel
{
public:
	virtual ~CBaseControlPanel();

	const wxString& GetPanelName() const { return m_szName; }

	const CHLMV* GetHLMV() const { return m_pHLMV; }
	CHLMV* GetHLMV() { return m_pHLMV; }

	virtual void InitializeUI() {}

	//Called right before the 3D view is updated.
	virtual void ViewPreUpdate() {}

	//Called when the 3D view is drawing.
	virtual void Draw3D( const wxSize& size ) {}

	//Called every time the 3D view is updated.
	virtual void ViewUpdated() {}

	//Called when the panel becomes the active panel.
	virtual void PanelActivated() {}

	//Called when the panel is no longer the active panel.
	virtual void PanelDeactivated() {}

protected:
	CBaseControlPanel( wxWindow* pParent, const wxString& szName, CHLMV* const pHLMV );

	/**
	*	Gets the window to use as the direct parent for child elements.
	*/
	wxWindow* GetElementParent() { return this; }

	/**
	*	Gets the main sizer to add elements to.
	*/
	wxSizer* GetMainSizer() { return m_pMainSizer; }

protected:
	CHLMV* const m_pHLMV;

private:
	const wxString m_szName;

	wxBoxSizer* m_pMainSizer;

private:
	CBaseControlPanel( const CBaseControlPanel& ) = delete;
	CBaseControlPanel& operator=( const CBaseControlPanel& ) = delete;
};
}

#endif //CONTROLPANELS_CBASECONTROLPANEL_H