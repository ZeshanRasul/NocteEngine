#include <assert.h>
#include "Application.h"

#define BIND_EVENT_FN(x) std::bind(&x, this, std::placeholders::_1)

Application* Application::s_Instance = nullptr;

Application::Application()	
{
	assert(!s_Instance);
	s_Instance = this;

	m_Window = std::unique_ptr<Window>(new Window());
	m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
	
	m_Hwnd = m_Window->GetWindowHandle();

	m_Renderer = std::unique_ptr<Renderer>(new Renderer(m_Hwnd, m_Window->GetWidth(), m_Window->GetHeight()));
};

Application::~Application()
{

}

void Application::OnEvent(Event& event)
{
	EventDispatcher dispatcher(event);
	dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));

}

int Application::Run()
{
	while (m_Running)
	{
		if (const auto ecode = Window::ProcessMessages())
		{
			return *ecode;
		}
		m_Renderer->Update();
		m_Renderer->Draw();
	}

	return 0;
}

bool Application::OnWindowClose(WindowCloseEvent& event)
{
	m_Running = false;
	return true;
}
