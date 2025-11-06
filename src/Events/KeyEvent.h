#pragma once
#include "Event.h"

class KeyEvent : public Event
{
public:
	inline int GetKeyCode() const { return m_KeyCode; }

	EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

protected:
	KeyEvent(int keyCode)
		:
		m_KeyCode(keyCode)
	{}

	int m_KeyCode;
};

class KeyPressedEvent : public KeyEvent
{
public:
	KeyPressedEvent(int keyCode, int repeatCount)
		:
		KeyEvent(keyCode),
		m_RepeatCount(repeatCount)
	{}

	inline int GetRepeatCount() const{ return m_RepeatCount; }

	EVENT_CLASS_TYPE(KeyPressed)

private:
	int m_RepeatCount;
};

class KeyReleasedEvent : public KeyEvent
{
public:
	KeyReleasedEvent(int keyCode)
		:
		KeyEvent(keyCode)
	{}

	EVENT_CLASS_TYPE(KeyReleased)
};

class KeyTypedEvent : public KeyEvent
{
public:
	KeyTypedEvent(int keyCode)
		:
		KeyEvent(keyCode)
	{}

	EVENT_CLASS_TYPE(KeyTyped)
};