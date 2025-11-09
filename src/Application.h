#pragma once
#include <memory>
#include <Windows.h>

#include "Events/Event.h"
#include "Events/ApplicationEvent.h"
#include "Events/MouseEvent.h"
#include "Window.h"
#include "Renderer/Renderer.h"

class Application
{
public:
	Application();
	virtual ~Application();

	int Run();
	
	void OnEvent(Event& event);

	inline static Application& Get() { return *s_Instance; }

	inline Window& GetWindow() { return *m_Window; }

	inline HWND GetHWND() { return m_Window->GetWindowHandle(); }

private:
	bool m_Running = true;
	bool OnWindowClose(WindowCloseEvent& event);

	std::unique_ptr<Window> m_Window;
	HWND m_Hwnd;

	std::unique_ptr<Renderer> m_Renderer;
	static Application* s_Instance;
};
