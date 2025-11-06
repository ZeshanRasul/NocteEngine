#pragma once
#include <optional>
#include <vector>
#include <queue>
#include <bitset>

class Input
{
	friend class Window;
public:
	class InputKeyboardEvent
	{
	public:
		enum class Type
		{
			Press,
			Release,
			Invalid
		};
	private:
		Type type;
		unsigned char code;
	public:
		InputKeyboardEvent()
			:
			type(Type::Invalid),
			code(0u)
		{}

		InputKeyboardEvent(Type type, unsigned char code)
			:
			type(type),
			code(code)
		{

		}

		bool IsPress() const
		{
			return type == Type::Press;
		}

		bool IsRelease() const 
		{
			return type == Type::Release;
		}

		bool IsValid() const
		{
			return type != Type::Invalid;
		}

		unsigned char GetCode() const
		{
			return code;
		}
	};
	
	class InputMouseEvent
	{
	public:
		enum class Type
		{
			LPress,
			LRelease,
			RPress,
			RRelease,
			WheelUp,
			WheelDown,
			Move,
			Invalid,
			Enter,
			Leave
		};

	private:
		Type type;
		bool isLeftPressed;
		bool isRightPressed;
		int x;
		int y;

	public:

		InputMouseEvent()
			:
			type(Type::Invalid),
			isLeftPressed(false),
			isRightPressed(false),
			x(0),
			y(0)
		{

		}

		InputMouseEvent(Type type, const Input& parent)
			:
			type(type),
			isLeftPressed(parent.isLeftPressed),
			isRightPressed(parent.isRightPressed),
			x(parent.x),
			y(parent.y)
		{}

		bool IsValid() const
		{
			return type != Type::Invalid;
		}

		Type GetType() const
		{
			return type;
		}

		std::pair<int, int> GetMousePos() const
		{
			return { x, y };
		}

		int GetMousePosX() const
		{
			return x;
		}

		int GetMousePosY() const
		{
			return y;
		}

		bool IsLeftPressed() const
		{
			return isLeftPressed;
		}

		bool IsRightPressed() const
		{
			return isRightPressed;
		}
	};

public:
	///////////////////////
	///KEYBOARD FUNCTIONS//
	///////////////////////

	bool IsKeyPressed(unsigned char keycode);
	std::optional<InputKeyboardEvent> ReadKey();
	bool IsKeyEmpty();
	void FlushKey();
	std::optional<char> ReadChar();
	bool IsCharEmpty();
	void FlushChar();
	void FlushKeyboard();
	void EnableAutorepeat();
	void DisableAutorepeat();
	bool IsAutorepeatEnabled();
	template<typename T>
	void TrimKeyboardBuffer(std::queue<T>& buffer);

	///////////////////////
	///MOUSE FUNCTIONS/////
	///////////////////////

	std::pair<int, int> GetMousePos();
	int GetMousePosX();
	int GetMousePosY();
	bool IsLeftPressed();
	bool IsRightPressed();
	bool IsInWindow();
	std::optional<InputMouseEvent> ReadMouse();
	bool IsMouseEmpty()
	{
		return mouseBuffer.empty();
	};
	void FlushMouse();

private:
	///////////////////////
	///KEYBOARD FUNCTIONS//
	///////////////////////

	void OnKeyPressed(unsigned char keycode);
	void OnKeyReleased(unsigned char keycode);
	void OnChar(char character);
	void ClearState();

	///////////////////////
	///MOUSE FUNCTIONS/////
	///////////////////////

	void OnMouseMove(int x, int y);
	void OnMouseEnter();
	void OnMouseLeave();
	void OnLeftPressed();
	void OnLeftReleased();
	void OnRightPressed();
	void OnRightReleased();
	void OnWheelUp();
	void OnWheelDown();
	void OnWheelDelta(int delta);
	void TrimMouseBuffer();

private:
	///////////////////////
	///KEYBOARD VARIABLES//
	///////////////////////

	static constexpr unsigned int nKeys = 256u;
	static constexpr unsigned int bufferSize = 16u;
	bool autorepeatEnabled = false;
	std::bitset<nKeys> keystates;
	std::queue<InputKeyboardEvent> keybuffer;
	std::queue<char> charbuffer;

	///////////////////////
	///MOUSE VARIABLES/////
	///////////////////////

	bool isLeftPressed = false;
	bool isRightPressed = false;
	int x;
	int y;
	bool isInWindow = false;
	int wheelDeltaCarry = 0;
	std::queue<InputMouseEvent> mouseBuffer;
};