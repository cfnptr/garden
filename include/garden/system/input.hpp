// Copyright 2022-2025 Nikita Fediuchin. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/***********************************************************************************************************************
 * @file
 * @brief Common user input functions.
 */

#pragma once
#include "garden/defines.hpp"
#include "ecsm.hpp"
#include "math/vector.hpp"

namespace garden
{

using namespace ecsm;

/***********************************************************************************************************************
 * @brief Keyboard button key codes. (GLFW)
 */
enum class KeyboardButton : int16
{
	Unknown = -1,
	Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
	N0 = 48, N1 = 49, N2 = 50, N3 = 51, N4 = 52, N5 = 53, N6 = 54, N7 = 55, N8 = 56, N9 = 57,
	Semicolon = 59, Equal = 61,
	A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72, I = 73, J = 74, K = 75, L = 76, M = 77, N = 78, 
	O = 79, P = 80, Q = 81, R = 82, S = 83, T = 84, U = 85, V = 86, W = 87, X = 88, Y = 89, Z = 90,
	LeftBracket = 91, Backslash = 92, RightBracket = 93, GraveAccent = 96,
	World1 = 161, World2 = 162,
	Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260, Delete = 261,
	Right = 262, Left = 263, Down = 264, Up = 265, PageUp = 266, PageDown = 267, Home = 268, End = 269,
	CapsLock = 280, ScrollLock = 281, NumLock = 282, PrintScreen = 283, Pause = 284,
	F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295, F7 = 296, F8 = 297, F9 = 298, F10 = 299, F11 = 300, 
	F12 = 301, F13 = 302, F14 = 303, F15 = 304, F16 = 305, F17 = 306, F18 = 307, F19 = 308, F20 = 309,
	F21 = 310, F22 = 311, F23 = 312, F24 = 313, F25 = 314,
	KP_0 = 320, KP_1 = 321, KP_2 = 322, KP_3 = 323, KP_4 = 324, KP_5 = 325, KP_6 = 326,
	KP_7 = 327, KP_8 = 328, KP_9 = 329, KP_Decimal = 330, KP_Divide = 331, KP_Multiply = 332, 
	KP_Subtract = 333, KP_Add = 334, KP_Enter = 335, KP_Equal = 336,
	LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
	RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347, Menu = 348,
	Last = Menu
};
/**
 * @brief Mouse button key codes. (GLFW)
 */
enum class MouseButton : uint8
{
	N1 = 0, N2 = 1, N3 = 2, N4 = 3, N5 = 4, N6 = 5, N7 = 6, N8 = 7,
	Last = N8, Left = N1, Right = N2, Middle = N3
};

/**
 * @brief Mouse cursor visibility modes. (GLFW)
 */
enum class CursorMode : uint8
{
	Normal = 0, Hidden = 1, Locked = 2, Count = 3
};
/**
 * @brief Mouse cursor visual types. (GLFW)
 */
enum class CursorType : uint8
{
	Default = 0, Arrow = 1, Ibeam = 2, Crosshair = 3, PointingHand = 4, ResizeEW = 5, 
	ResizeNS = 6, ResizeNWSE = 7, ResizeNESW = 8, ResizeAll = 9, NotAllowed = 10, Count = 11,
};

/***********************************************************************************************************************
 * @brief Defined keyboard button count. (GLFW)
 */
constexpr uint8 keyboardButtonCount = 120;

/**
 * @brief All defined keyboard buttons array. (GLFW)
 */
constexpr KeyboardButton allKeyboardButtons[keyboardButtonCount] =
{
	KeyboardButton::Space, KeyboardButton::Apostrophe, KeyboardButton::Comma,
	KeyboardButton::Minus, KeyboardButton::Period, KeyboardButton::Slash,
	KeyboardButton::N0, KeyboardButton::N1, KeyboardButton::N2, KeyboardButton::N3, KeyboardButton::N4,
	KeyboardButton::N5, KeyboardButton::N6, KeyboardButton::N7, KeyboardButton::N8, KeyboardButton::N9,
	KeyboardButton::Semicolon, KeyboardButton::Equal,
	KeyboardButton::A, KeyboardButton::B, KeyboardButton::C, KeyboardButton::D, KeyboardButton::E, KeyboardButton::F,
	KeyboardButton::G, KeyboardButton::H, KeyboardButton::I, KeyboardButton::J, KeyboardButton::K, KeyboardButton::L,
	KeyboardButton::M, KeyboardButton::N, KeyboardButton::O, KeyboardButton::P, KeyboardButton::Q, KeyboardButton::R, 
	KeyboardButton::S, KeyboardButton::T, KeyboardButton::U, KeyboardButton::V, KeyboardButton::W, KeyboardButton::X,
	KeyboardButton::Y, KeyboardButton::Z,
	KeyboardButton::LeftBracket, KeyboardButton::Backslash, KeyboardButton::RightBracket, KeyboardButton::GraveAccent,
	KeyboardButton::World1, KeyboardButton::World2,
	KeyboardButton::Escape, KeyboardButton::Enter, KeyboardButton::Tab, KeyboardButton::Backspace,
	KeyboardButton::Insert, KeyboardButton::Delete, KeyboardButton::Right, KeyboardButton::Left, KeyboardButton::Down,
	KeyboardButton::Up, KeyboardButton::PageUp, KeyboardButton::PageDown, KeyboardButton::Home, KeyboardButton::End,
	KeyboardButton::CapsLock, KeyboardButton::ScrollLock, KeyboardButton::NumLock, KeyboardButton::PrintScreen,
	KeyboardButton::Pause,
	KeyboardButton::F1, KeyboardButton::F2, KeyboardButton::F3, KeyboardButton::F4, KeyboardButton::F5,
	KeyboardButton::F6, KeyboardButton::F7, KeyboardButton::F8, KeyboardButton::F9, KeyboardButton::F10,
	KeyboardButton::F11, KeyboardButton::F12, KeyboardButton::F13, KeyboardButton::F14, KeyboardButton::F15,
	KeyboardButton::F16, KeyboardButton::F17, KeyboardButton::F18, KeyboardButton::F19, KeyboardButton::F20,
	KeyboardButton::F21, KeyboardButton::F22, KeyboardButton::F23, KeyboardButton::F24, KeyboardButton::F25,
	KeyboardButton::KP_0, KeyboardButton::KP_1, KeyboardButton::KP_2, KeyboardButton::KP_3, KeyboardButton::KP_4,
	KeyboardButton::KP_5, KeyboardButton::KP_6, KeyboardButton::KP_7, KeyboardButton::KP_8, KeyboardButton::KP_9,
	KeyboardButton::KP_Decimal, KeyboardButton::KP_Divide, KeyboardButton::KP_Multiply, KeyboardButton::KP_Subtract,
	KeyboardButton::KP_Add, KeyboardButton::KP_Enter, KeyboardButton::KP_Equal,
	KeyboardButton::LeftShift, KeyboardButton::LeftControl, KeyboardButton::LeftAlt, KeyboardButton::LeftSuper,
	KeyboardButton::RightShift, KeyboardButton::RightControl, KeyboardButton::RightAlt, KeyboardButton::RightSuper,
	KeyboardButton::Menu,
};

/***********************************************************************************************************************
 * @brief Handles input from user.
 * 
 * @details
 * The input system is responsible for detecting user actions (e.g., key presses, mouse movements, 
 * touch gestures) and translating these into variables or events within a game or application.
 * 
 * Registers events: Input, Output, FileDrop.
 */
class InputSystem final : public System, public Singleton<InputSystem>
{
public:
	/**
	 * @brief Default window width in pixels across X-axis.
	 */
	static constexpr uint32 defaultWindowWidth = 1280;
	/**
	 * @brief Default window height in pixels across Y-axis.
	 */
	static constexpr uint32 defaultWindowHeight = 720;
	/**
	 * @brief Default window size in pixels.
	 */
	static constexpr uint2 defaultWindowSize = uint2(defaultWindowWidth, defaultWindowHeight);
private:
	mutex eventLocker;
	vector<void*> standardCursors;
	vector<bool> newKeyboardStates;
	vector<bool> lastKeyboardStates;
	vector<bool> currKeyboardStates;
	vector<bool> newMouseStates;
	vector<bool> lastMouseStates;
	vector<bool> currMouseStates;
	vector<uint32> accumKeyboardChars;
	vector<uint32> newKeyboardChars;
	vector<uint32> currKeyboardChars;
	vector<fs::path> accumFileDrops;
	vector<fs::path> newFileDrops;
	vector<fs::path> currFileDrops;
	vector<string> newWindowIconPaths;
	vector<string> currWindowIconPaths;
	string newWindowTitle;
	string currWindowTitle;
	string newClipboard;
	string lastClipboard;
	string currClipboard;
	uint2 newFramebufferSize = uint2(0);
	uint2 currFramebufferSize = uint2(0);
	uint2 newWindowSize = uint2(0);
	uint2 currWindowSize = uint2(0);
	float2 newContentScale = float2(0.0);
	float2 currContentScale = float2(0.0);
	float2 newCursorPos = float2(0.0f);
	float2 currCursorPos = float2(0.0f);
	float2 cursorDelta = float2(0.0f);
	float2 accumMouseScroll = float2(0.0f);
	float2 newMouseScroll = float2(0.0f);
	float2 currMouseScroll = float2(0.0f);
	double time = 0.0, systemTime = 0.0, deltaTime = 0.0;
	const fs::path* currFileDropPath = nullptr;
	CursorMode newCursorMode = CursorMode::Normal;
	CursorMode currCursorMode = CursorMode::Normal;
	CursorType newCursorType = CursorType::Default;
	CursorType currCursorType = CursorType::Default;
	bool newCursorInWindow = false;
	bool lastCursorInWindow = false;
	bool currCursorInWindow = false;
	bool newWindowInFocus = false;
	bool lastWindowInFocus = false;
	bool currWindowInFocus = false;
	bool hasNewClipboard = false;

	/**
	 * @brief Creates a new input system instance.
	 * @param setSingleton set system singleton instance
	 */
	InputSystem(bool setSingleton = true);
	/**
	 * @brief Destroys input system instance.
	 */
	~InputSystem() final;

	void preInit();
	void deinit();
	void input();
	void output();

	static void onKeyboardButton(void* window, int key, int scancode, int action, int mods);
	static void onMouseScroll(void* window, double offsetX, double offsetY);
	static void onFileDrop(void* window, int count, const char** paths);
	static void onKeyboardChar(void* window, unsigned int codepoint);

	static void renderThread();
	friend class ecsm::Manager;
public:
	/*******************************************************************************************************************
	 * @brief Current time multiplier.
	 * @details Can be used to simulate slow motion or speed up effects.
	 */
	double timeMultiplier = 1.0;

	/**
	 * @brief Returns time since start of the program. (in seconds)
	 * @details You can use it to implement time based events or delays.
	 * @note It is affected by the timeMultiplier value.
	 */
	double getTime() const noexcept { return time; }
	/**
	 * @brief Returns time elapsed between two previous frames. (in seconds)
	 * @note It is affected by the timeMultiplier value.
	 * 
	 * @details
	 * This value is crucial for ensuring that animations, physics calculations, 
	 * and game logic run smoothly and consistently, regardless of the frame rate.
	 */
	double getDeltaTime() const noexcept { return deltaTime; }

	/**
	 * @brief Returns current window framebuffer size in pixels.
	 * @details It can change when window is resized or minified.
	 */
	uint2 getFramebufferSize() const noexcept { return currFramebufferSize; }
	/**
	 * @brief Returns current window size in units.
	 * @note It can differ from the framebuffer size! (eg. on macOS)
	 */
	uint2 getWindowSize() const noexcept { return currWindowSize; }
	/**
	 * @brief Returns current windows content scale factor.
	 * @details It can change by the display settings.
	 */
	float2 getContentScale() const noexcept { return currContentScale; }

	/**
	 * @brief Is windows currently in focus.
	 */
	bool isWindowInFocus() const noexcept { return currWindowInFocus; }
	/**
	 * @brief Has the window been focused.
	 */
	bool isWindowFocused() const noexcept { return lastWindowInFocus != currWindowInFocus && currWindowInFocus; }
	/**
	 * @brief Has the window been unfocused.
	 */
	bool isWindowUnfocused() const noexcept { return lastWindowInFocus != currWindowInFocus && !currWindowInFocus; }

	/**
	 * @brief Returns current cursor position in the window. (in units)
	 * @details Useful for implementing FPS controller, inventory.
	 */
	float2 getCursorPosition() const noexcept { return currCursorPos; }
	/**
	 * @brief Returns current cursor delta position in the window. (in units)
	 * @details Useful for implementing FPS controller, inventory.
	 */
	float2 getCursorDelta() const noexcept { return cursorDelta; }

	/**
	 * @brief Is cursor directly over the window content area.
	 */
	bool isCursorInWindow() const noexcept { return currCursorInWindow; }
	/**
	 * @brief Has cursor entered the window content area.
	 */
	bool isCursorEntered() const noexcept { return lastCursorInWindow != currCursorInWindow && currCursorInWindow; }
	/**
	 * @brief Has cursor leaved the window content area.
	 */
	bool isCursorLeaved() const noexcept { return lastCursorInWindow != currCursorInWindow && !currCursorInWindow; }

	/**
	 * @brief Returns current mouse delta scroll. (in units)
	 * @details Useful for implementing FPS controller, inventory.
	 */
	float2 getMouseScroll() const noexcept { return currMouseScroll; }

	/**
	 * @brief Has the keyboard button been pressed.
	 * @param button target keyboard button
	 */
	bool isKeyboardPressed(KeyboardButton button) const noexcept
	{
		GARDEN_ASSERT((int16)button >= 0 && (int16)button <= (int16)KeyboardButton::Last);
		return !lastKeyboardStates[(int16)button] && currKeyboardStates[(int16)button];
	}
	/**
	 * @brief Has the keyboard button been released.
	 * @param button target keyboard button
	 */
	bool isKeyboardReleased(KeyboardButton button) const noexcept
	{
		GARDEN_ASSERT((int16)button >= 0 && (int16)button <= (int16)KeyboardButton::Last);
		return lastKeyboardStates[(int)button] && !currKeyboardStates[(int)button];
	}

	/**
	 * @brief Has the mouse button been pressed.
	 * @param button target mouse button
	 */
	bool isMousePressed(MouseButton button) const noexcept
	{
		GARDEN_ASSERT((uint8)button <= (uint8)MouseButton::Last);
		return !lastMouseStates[(uint8)button] && currMouseStates[(uint8)button];
	}
	/**
	 * @brief Has the mouse button been released.
	 * @param button target mouse button
	 */
	bool isMouseReleased(MouseButton button) const noexcept
	{
		GARDEN_ASSERT((uint8)button <= (uint8)MouseButton::Last);
		return lastMouseStates[(uint8)button] && !currMouseStates[(uint8)button];
	}

	/**
	 * @brief Is keyboard button in the pressed state.
	 * @param button target keyboard button
	 */
	bool getKeyboardState(KeyboardButton button) const noexcept
	{
		GARDEN_ASSERT((int16)button >= 0 && (int16)button <= (int16)KeyboardButton::Last);
		return currKeyboardStates[(int16)button];
	}
	/**
	 * @brief Is mouse button in the pressed state.
	 * @param button target keyboard button
	 */
	bool getMouseState(MouseButton button) const noexcept
	{
		GARDEN_ASSERT((uint8)button <= (uint8)MouseButton::Last);
		return currMouseStates[(uint8)button];
	}

	/**
	 * @brief Returns current mouse cursor mode.
	 * @details Useful for hiding mouse cursor.
	 */
	CursorMode getCursorMode() const noexcept { return newCursorMode; }
	/**
	 * @brief Sets mouse cursor mode.
	 * @param mode target cursor mode
	 */
	void setCursorMode(CursorMode mode) noexcept
	{
		GARDEN_ASSERT((uint8)mode < (uint8)CursorMode::Count);
		newCursorMode = mode;
	}

	/**
	 * @brief Returns current mouse cursor type.
	 */
	CursorType getCursorType() const noexcept { return newCursorType; }
	/**
	 * @brief Sets mouse cursor mode.
	 * @param mode target cursor mode
	 */
	void setCursorType(CursorType type) noexcept
	{
		GARDEN_ASSERT((uint8)type < (uint8)CursorType::Count);
		newCursorType = type;
	}

	// TODO: allow to create custom image cursor.

	/**
	 * @brief Sets window title. (UTF-8)
	 * @param title target title string
	 */
	void setWindowTitle(string_view title) noexcept { newWindowTitle = title; }
	/**
	 * @brief Sets window icon images.
	 * @param paths target icon paths
	 * @throw GardenError if icons are not supported on a target platform.
	 */
	void setWindowIcon(const vector<string>& paths);

	/**
	 * @brief Returns current clipboard string.
	 */
	const string& getClipboard() const noexcept { return newClipboard; }
	/**
	 * @brief Sets clipboard string.
	 * @param clipboard target clipboard string
	 */
	void setClipboard(string_view clipboard) noexcept
	{
		newClipboard = clipboard;
		hasNewClipboard = true;
	}

	/**
	 * @brief Returns current keyboard text input array. (UTF-32 encoded)
	 */
	const vector<uint32>& getKeyboardChars32() const noexcept { return currKeyboardChars; }
	/**
	 * @brief Returns current keyboard text input array. (UTF-8 encoded)
	 */
	const string& getKeyboardChars() const noexcept { abort(); } // TODO: decode UTF-32 buffer

	/**
	 * @brief Returns current dropped file path.
	 * @note Use it on "FileDrop" event.
	 */
	const fs::path& getCurrentFileDropPath() const noexcept { return *currFileDropPath; }

	/**
	 * @brief Creates and starts separate render thread.
	 * 
	 * @param manager global manager instance
	 * @param renderThread returned render thread
	 */
	static void startRenderThread();
};

/***********************************************************************************************************************
 * @brief Converts time in seconds to milliseconds.
 * @param time target time in seconds
 */
static constexpr double timeToMilliseconds(double time) noexcept { return time * 1000.0; }
/**
 * @brief Converts time in seconds to minutes.
 * @param time target time in seconds
 */
static constexpr double timeToMinutes(double time) noexcept { return time / 60.0; }
/**
 * @brief Converts time in seconds to hours.
 * @param time target time in seconds
 */
static constexpr double timeToHours(double time) noexcept { return time / 3600.0; }
/**
 * @brief Converts time in seconds to days.
 * @param time target time in seconds
 */
static constexpr double timeToDays(double time) noexcept { return time / 86400.0; }

} // namespace garden