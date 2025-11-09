#pragma once
#include <Windows.h>
#include <optional>
#include "Events/Event.h"
#include "Input.h"

struct WindowProps
{
	std::string Title;
	unsigned int Width;
	unsigned int Height;

	WindowProps(const std::string& title = "Windows Framework", 
		unsigned int width = 1280,
		unsigned int height = 960)
		:
		Title(title),
		Width(width),
		Height(height)
	{}
};

class Window
{
public:
	using EventCallbackFn = std::function<void(Event&)>;

private:
	class WindowClass
	{
	public:
		static const wchar_t* GetName();
		static HINSTANCE GetInstance();

	private:
		WindowClass();
		~WindowClass();
		
		static constexpr const wchar_t* wndClassName = L"Windows Framework";
		static WindowClass wndClass;
		HINSTANCE hInstance;
	};

public:
	Window(const WindowProps& props = WindowProps());
	~Window();

	void OnUpdate();

	inline unsigned int GetWidth() const { return m_Data.Width; }
	inline unsigned int GetHeight() const { return m_Data.Height; }

	inline void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }
	void SetVSync(bool enabled);
	bool IsVSync() const;
	static std::optional<int> ProcessMessages();

	inline virtual void* GetNativeWindow() const { return m_Window; }
	HWND GetWindowHandle() { return m_Hwnd; }

private:
	struct WindowData
	{
		std::string Title;
		unsigned int Width, Height;
		bool VSync;

		EventCallbackFn EventCallback;
	};

	void Init(const WindowProps& props = WindowProps());
	void Shutdown();

	static LRESULT CALLBACK HandleMessageSetup(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK HandleMessageThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

public:
	Input input;
	bool m_Raster = true;

private:
	void* m_Window;

	WindowData m_Data;

	HWND m_Hwnd;

};