#include <Windows.h>
#include "Input.h"

///////////////////////
///KEYBOARD FUNCTIONS//
///////////////////////

bool Input::IsKeyPressed(unsigned char keycode)
{
	return keystates[keycode];
}

std::optional<Input::InputKeyboardEvent> Input::ReadKey()
{
	if (keybuffer.size() > 0)
	{
		InputKeyboardEvent event = keybuffer.front();
		keybuffer.pop();
		return event;
	}
	return {};
}

bool Input::IsKeyEmpty()
{
	return keybuffer.empty();
}

void Input::FlushKey()
{
	keybuffer = std::queue<InputKeyboardEvent>();
}

std::optional<char> Input::ReadChar()
{
	if (charbuffer.size() > 0)
	{
		unsigned char charcode = charbuffer.front();
		charbuffer.pop();
		return charcode;
	}
	return {};
}

bool Input::IsCharEmpty()
{
	return charbuffer.empty();
}

void Input::FlushChar()
{
	charbuffer = std::queue<char>();
}

void Input::EnableAutorepeat()
{
	autorepeatEnabled = true;
}

void Input::DisableAutorepeat()
{
	autorepeatEnabled = false;
}

bool Input::IsAutorepeatEnabled()
{
	return autorepeatEnabled;
}

void Input::OnKeyPressed(unsigned char keycode)
{
	keystates[keycode] = true;
	keybuffer.push(InputKeyboardEvent(InputKeyboardEvent::Type::Press, keycode));
	TrimKeyboardBuffer(keybuffer);
}

void Input::OnKeyReleased(unsigned char keycode)
{
	keystates[keycode] = false;
	keybuffer.push(InputKeyboardEvent(InputKeyboardEvent::Type::Release, keycode));
	TrimKeyboardBuffer(keybuffer);
}

void Input::OnChar(char character)
{
	charbuffer.push(character);
	TrimKeyboardBuffer(charbuffer);
}

void Input::ClearState()
{
	keystates.reset();
}

template<typename T>
void Input::TrimKeyboardBuffer(std::queue<T>& buffer)
{
	while (buffer.size() > bufferSize)
	{
		buffer.pop();
	}
}

///////////////////////
///MOUSE FUNCTIONS/////
///////////////////////

std::pair<int, int> Input::GetMousePos()
{
	return { x, y };
}

int Input::GetMousePosX()
{
	return x;
}

int Input::GetMousePosY()
{
	return y;
}

bool Input::IsLeftPressed()
{
	return isLeftPressed;
}

bool Input::IsRightPressed()
{
	return isRightPressed;
}

bool Input::IsInWindow()
{
	return isInWindow;
}

std::optional<Input::InputMouseEvent> Input::ReadMouse()
{
	if (mouseBuffer.size() > 0)
	{
		InputMouseEvent event = mouseBuffer.front();
		mouseBuffer.pop();
		return event;
	}
	return {};
}

void Input::FlushMouse()
{
	mouseBuffer = std::queue<InputMouseEvent>();
}

void Input::OnMouseMove(int newX, int newY)
{
	x = newX;
	y = newY;

	mouseBuffer.push(InputMouseEvent(InputMouseEvent::Type::Move, *this));
	TrimMouseBuffer();
}

void Input::OnMouseLeave()
{
	isInWindow = false;
	mouseBuffer.push(InputMouseEvent(InputMouseEvent::Type::Leave, *this));
	TrimMouseBuffer();
}

void Input::OnMouseEnter()
{
	isInWindow = true;
	mouseBuffer.push(InputMouseEvent(InputMouseEvent::Type::Enter, *this));
	TrimMouseBuffer();
}

void Input::OnLeftPressed()
{
	isLeftPressed = true;
	mouseBuffer.push(InputMouseEvent(InputMouseEvent::Type::LPress, *this));
	TrimMouseBuffer();
}

void Input::OnLeftReleased()
{
	isLeftPressed = false;
	mouseBuffer.push(InputMouseEvent(InputMouseEvent::Type::LRelease, *this));
	TrimMouseBuffer();
}

void Input::OnRightPressed()
{
	isRightPressed = true;
	mouseBuffer.push(InputMouseEvent(InputMouseEvent::Type::RPress, *this));
	TrimMouseBuffer();
}

void Input::OnRightReleased()
{
	isRightPressed = false;
	mouseBuffer.push(InputMouseEvent(InputMouseEvent::Type::RRelease, *this));
	TrimMouseBuffer();
}

void Input::OnWheelUp()
{
	mouseBuffer.push(InputMouseEvent(InputMouseEvent::Type::WheelUp, *this));
	TrimMouseBuffer();
}

void Input::OnWheelDown()
{
	mouseBuffer.push(InputMouseEvent(InputMouseEvent::Type::WheelDown, *this));
	TrimMouseBuffer();
}

void Input::OnWheelDelta(int delta)
{
	wheelDeltaCarry += delta;

	while (wheelDeltaCarry >= WHEEL_DELTA)
	{
		wheelDeltaCarry -= WHEEL_DELTA;
		OnWheelUp();
	}

	while (wheelDeltaCarry <= WHEEL_DELTA)
	{
		wheelDeltaCarry += WHEEL_DELTA;
		OnWheelDown();
	}
}

void Input::TrimMouseBuffer()
{
	while (mouseBuffer.size() > bufferSize)
	{
		mouseBuffer.pop();
	}

}