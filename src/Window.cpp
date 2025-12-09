#include "Window.h"
#include "Events/ApplicationEvent.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include "Renderer/Renderer.h"
#include "manipulator.h"
#include "Windowsx.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx12.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Window::WindowClass Window::WindowClass::wndClass;

Window::WindowClass::WindowClass()
	:
	hInstance(GetModuleHandle(nullptr))
{
	WNDCLASSEX wc = { 0 };
	wc.cbSize = sizeof(wc);
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = HandleMessageSetup;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = GetInstance();
	wc.hIcon = nullptr;
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = nullptr;
	wc.lpszMenuName = nullptr;
	wc.lpszClassName = GetName();
	wc.hIconSm = nullptr;

	RegisterClassEx(&wc);
}

Window::WindowClass::~WindowClass()
{
	UnregisterClass(wndClassName, GetInstance());
}

const wchar_t* Window::WindowClass::GetName()
{
	return wndClassName;
}

HINSTANCE Window::WindowClass::GetInstance()
{
	return wndClass.hInstance;
}

Window::Window(Camera& cam, GameTimer& gt, const WindowProps& props)
{
	m_Camera = cam;
	m_GameTimer = gt;
	Init(props);
}

Window::~Window()
{
	Shutdown();
}

void Window::Init(const WindowProps& props)
{
	m_Camera.SetPosition(0.0f, 55.0f, -115.0f);

	m_Data.Title = props.Title;
	m_Data.Width = props.Width;
	m_Data.Height = props.Height;

	RECT wr;
	wr.left = 100;
	wr.right = m_Data.Width + wr.left;
	wr.top = 100;
	wr.bottom = m_Data.Height + wr.top;
	AdjustWindowRect(&wr, WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU, FALSE);

	const wchar_t* pWindowName = L"Nocte Engine";

	m_Hwnd = CreateWindowEx(
		0, WindowClass::GetName(),
		pWindowName,
		WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
		CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
		nullptr, nullptr, WindowClass::GetInstance(), this
	);

	ShowWindow(m_Hwnd, SW_SHOWDEFAULT);
	HCURSOR cursor = LoadCursor(0, IDC_ARROW);
	SetCursor(cursor);

	SetVSync(true);
}

void Window::Shutdown()
{
	DestroyWindow(m_Hwnd);
}

void Window::OnUpdate()
{

}

void Window::SetVSync(bool enabled)
{
	if (enabled)
	{

	}
	else
	{

	}
	m_Data.VSync = enabled;
}

bool Window::IsVSync() const
{
	return m_Data.VSync;
}

std::optional<int> Window::ProcessMessages()
{
	MSG msg;

	while (PeekMessage(&msg, nullptr, 0u, 0u, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
		{
			return (int)msg.wParam;
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);

		return {};
	}

	return {};
}

LRESULT CALLBACK Window::HandleMessageSetup(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_NCCREATE)
	{
		const CREATESTRUCT* const p_CreationData = reinterpret_cast<CREATESTRUCT*>(lParam);
		Window* const p_Wnd = static_cast<Window*>(p_CreationData->lpCreateParams);

		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p_Wnd));

		SetWindowLongPtr(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Window::HandleMessageThunk));

		return p_Wnd->HandleMessage(hWnd, msg, wParam, lParam);
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK Window::HandleMessageThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	Window* const p_Wnd = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	return p_Wnd->HandleMessage(hWnd, msg, wParam, lParam);
}

LRESULT Window::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	const float dt = m_GameTimer.DeltaTime();

	switch (msg)
	{


	case WM_CLOSE:
	{
		Window* const p_Wnd = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		WindowData& data = p_Wnd->m_Data;

		WindowCloseEvent event;
		if (data.EventCallback != nullptr)
		{
			data.EventCallback(event);
		}
		PostQuitMessage(0);
		break;
	}

	case WM_KILLFOCUS:
	{
		input.ClearState();
		break;
	}

	///////////////////////
	///KEYBOARD Messages///
	///////////////////////

	case WM_KEYDOWN:
	{
		switch (wParam)
		{

		case VK_SPACE:
		{
			m_Raster = !m_Raster;
			break;
		}
		case VK_ESCAPE:
		{
			PostQuitMessage(0);
			break;
		}


		break;
		}
		//case WM_SYSKEYDOWN:
		//{
		//	if (!(lParam & 0x40000000) || input.IsAutorepeatEnabled())
		//	{
		//		input.OnKeyPressed(static_cast<unsigned char>(wParam));
		//	}

		//	Window* const p_Wnd = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		//	WindowData& data = p_Wnd->m_Data;

		//	KeyPressedEvent event((int)wParam, 0);
		//	if (data.EventCallback != nullptr)
		//	{
		//		data.EventCallback(event);
		//	}
		//	break;
		//}

	case WM_KEYUP:
		//case WM_SYSKEYUP:
		//{
		//	input.OnKeyReleased(static_cast<unsigned char>(wParam));

		//	Window* const p_Wnd = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		//	WindowData& data = p_Wnd->m_Data;

		//	KeyReleasedEvent event((int)wParam);
		//	if (data.EventCallback != nullptr)
		//	{
		//		data.EventCallback(event);
		//	}
		//	break;
		//}

	case WM_CHAR:
	{
		input.OnChar(static_cast <unsigned char>(wParam));
		break;
	}

	///////////////////////
	///MOUSE MESSAGES//////
	///////////////////////
	///////////////////////
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_MOUSEMOVE:
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_MOVE:
	{

	}

	case WM_MOUSEWHEEL:
	{
		const POINTS pt = MAKEPOINTS(lParam);

		Window* const p_Wnd = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		WindowData& data = p_Wnd->m_Data;

		MouseScrolledEvent event(pt.x, pt.y);
		if (data.EventCallback != nullptr)
		{
			data.EventCallback(event);
		}
		const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
		input.OnWheelDelta(delta);
		break;
	}

	case WM_SIZE:
	{
		UINT width = LOWORD(lParam);
		UINT height = HIWORD(lParam);

		Window* const p_Wnd = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
		WindowData& data = p_Wnd->m_Data;

		data.Width = width;
		data.Height = height;

		WindowResizeEvent event(width, height);
		if (data.EventCallback != nullptr)
		{
			data.EventCallback(event);
		}
		break;
	}

	}

	}
	return DefWindowProc(hWnd, msg, wParam, lParam);

	
}
	void Window::OnMouseDown(WPARAM btnState, int x, int y)
	{
		m_LastMousePos.x = x;
		m_LastMousePos.y = y;

		SetCapture(m_Hwnd);
	}

	void Window::OnMouseUp(WPARAM btnState, int x, int y)
	{
		ReleaseCapture();
	}

	void Window::OnMouseMove(WPARAM btnState, int x, int y)
	{
		if ((btnState & MK_LBUTTON) != 0)
		{
			// Make each pixel correspond to a quarter of a degree.
			float dx = XMConvertToRadians(0.1f * static_cast<float>(x - m_LastMousePos.x));
			float dy = XMConvertToRadians(0.1f * static_cast<float>(y - m_LastMousePos.y));

			m_Camera.Pitch(dy);
			m_Camera.RotateY(dx);
		//	m_Camera.UpdateViewMatrix();
		}

		m_LastMousePos.x = x;
		m_LastMousePos.y = y;
	}

	void Window::OnKeyboardInput(GameTimer & gt)
	{
		const float dt = gt.DeltaTime();

		if (GetAsyncKeyState('W') & 0x8000)
			m_Camera.Walk(30.0f * dt);

		if (GetAsyncKeyState('S') & 0x8000)
			m_Camera.Walk(-30.0f * dt);

		if (GetAsyncKeyState('A') & 0x8000)
			m_Camera.Strafe(-30.0f * dt);

		if (GetAsyncKeyState('D') & 0x8000)
			m_Camera.Strafe(30.0f * dt);

		m_Camera.UpdateViewMatrix();
	}



