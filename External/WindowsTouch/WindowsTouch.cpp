/*
* @author Valentin Simonov / http://va.lent.in/
*/

#include "WindowsTouch.h"

#define MAX_WINDOWS	3

PointerDelegatePtr			_delegate;
LogFuncPtr					_log;
WindowData					_windows[MAX_WINDOWS];
TOUCH_API					_api;

void log(const wchar_t* str);
LRESULT CALLBACK wndProc8(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK wndProc7(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void decodeWin8Touches(WindowData *window, UINT msg, WPARAM wParam, LPARAM lParam);
void decodeWin7Touches(WindowData *window, UINT msg, WPARAM wParam, LPARAM lParam);
void registerWindow(HWND window, int i);
WindowData *lookupWindowByHandle(HWND hwnd);
bool isFullscreen(HWND window);
HWND findNewWindow();

extern "C" 
{

	void __stdcall Init(TOUCH_API api, LogFuncPtr logFunc, PointerDelegatePtr delegate)
	{
		_log = logFunc;
		_delegate = delegate;
		_api = api;
	}

	void __stdcall ActivateTouchDisplay(int displayIndex, int screenWidth, int screenHeight)
	{
		HWND window = findNewWindow();
		registerWindow(window, displayIndex);
		SetScreenParams(displayIndex, screenWidth, screenHeight);
	}

	void __stdcall Dispose()
	{
		for (int i = 0; i < MAX_WINDOWS; i++)
		{
			WindowData *window = &_windows[i];
			if (window->oldWindowProc)
			{
				SetWindowLongPtr(window->handle, GWLP_WNDPROC, (LONG_PTR)window->oldWindowProc);
				window->oldWindowProc = NULL;
				if (_api == WIN7)
				{
					UnregisterTouchWindow(window->handle);
				}
				window->handle = NULL;
			}
		}
	}

	void __stdcall SetScreenParams(int displayIndex, int screenWidth, int screenHeight)
	{
		WindowData *window = &_windows[displayIndex];

		if (!window->handle)
		{
			//TODO: Log message that the handle isn't initialized yet
			return;
		}

		window->screenWidth = screenWidth;
		window->screenHeight = screenHeight;

		HMONITOR monitor = MonitorFromWindow(window->handle, MONITOR_DEFAULTTONULL);
		MONITORINFO monitorInfo;
		if (GetMonitorInfo(monitor, &monitorInfo))
		{
			int nativeWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
			int nativeHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;

			if (isFullscreen(window->handle))
			{
				float scale = max(screenWidth / ((float)nativeWidth), screenHeight / ((float)nativeHeight));
				window->offsetX = (nativeWidth - screenWidth / scale) * .5f;
				window->offsetY = (nativeHeight - screenHeight / scale) * .5f;
				window->scaleX = scale;
				window->scaleY = scale;
			}
			else
			{
				window->offsetX = 0;
				window->offsetY = 0;
				window->scaleX = 1;
				window->scaleY = 1;
			}
		}
		else
		{
			//TODO: Log problem getting monitor info
		}
	}

	void __stdcall SetScreenTouchProperties(LPCSTR lpString, HANDLE hData)
	{
		for (int i = 0; i < MAX_WINDOWS; i++)
		{
			WindowData *window = &_windows[i];
			if (window->handle) {
				SetPropA(window->handle, lpString, hData);
			}
		}
	}

	void __stdcall RemoveScreenTouchProperties(LPCSTR lpString)
	{
		for (int i = 0; i < MAX_WINDOWS; i++)
		{
			WindowData *window = &_windows[i];
			if (window->handle) {
				RemovePropA(window->handle, lpString);
			}
		}
	}
}

LRESULT CALLBACK wndProc8(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WindowData *window = lookupWindowByHandle(hwnd);

	switch (msg)
	{
	case WM_TOUCH:
		CloseTouchInputHandle((HTOUCHINPUT)lParam);
		break;
	case WM_POINTERENTER:
	case WM_POINTERLEAVE:
	case WM_POINTERDOWN:
	case WM_POINTERUP:
	case WM_POINTERUPDATE:
	case WM_POINTERCAPTURECHANGED:
		decodeWin8Touches(window, msg, wParam, lParam);
		break;
	default:
		if (window)
		{
			return CallWindowProc((WNDPROC)window->oldWindowProc, hwnd, msg, wParam, lParam);
		}
	}
	return 0;
}

LRESULT CALLBACK wndProc7(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WindowData *window = lookupWindowByHandle(hwnd);

	switch (msg)
	{
	case WM_TOUCH:
		decodeWin7Touches(window, msg, wParam, lParam);
		break;
	default:
		if (window)
		{
			return CallWindowProc((WNDPROC)window->oldWindowProc, hwnd, msg, wParam, lParam);
		}
	}
	return 0;
}

void decodeWin8Touches(WindowData *window, UINT msg, WPARAM wParam, LPARAM lParam)
{
	//TODO: Attribute touch to source display instead of focused window
	int pointerId = GET_POINTERID_WPARAM(wParam);

	POINTER_INFO pointerInfo;
	if (!GetPointerInfo(pointerId, &pointerInfo)) return;

	POINT p;
	p.x = pointerInfo.ptPixelLocation.x;
	p.y = pointerInfo.ptPixelLocation.y;
	ScreenToClient(window->handle, &p);

	Vector2 position = Vector2(((float)p.x - window->offsetX) * window->scaleX, window->screenHeight - ((float)p.y - window->offsetY) * window->scaleY);
	PointerData data {};
	data.pointerFlags = pointerInfo.pointerFlags;
	data.changedButtons = pointerInfo.ButtonChangeType;

	if ((pointerInfo.pointerFlags & POINTER_FLAG_CANCELED) != 0
		|| msg == WM_POINTERCAPTURECHANGED) msg = POINTER_CANCELLED;

	switch (pointerInfo.pointerType)
	{
	case PT_MOUSE:
		break;
	case PT_TOUCH:
		POINTER_TOUCH_INFO touchInfo;
		GetPointerTouchInfo(pointerId, &touchInfo);
		data.flags = touchInfo.touchFlags;
		data.mask = touchInfo.touchMask;
		data.rotation = touchInfo.orientation;
		data.pressure = touchInfo.pressure;
		break;
	case PT_PEN:
		POINTER_PEN_INFO penInfo;
		GetPointerPenInfo(pointerId, &penInfo);
		data.flags = penInfo.penFlags;
		data.mask = penInfo.penMask;
		data.rotation = penInfo.rotation;
		data.pressure = penInfo.pressure;
		data.tiltX = penInfo.tiltX;
		data.tiltY = penInfo.tiltY;
		break;
	}

	_delegate(window->displayIndex, pointerId, msg, pointerInfo.pointerType, position, data);
}

void decodeWin7Touches(WindowData *window, UINT msg, WPARAM wParam, LPARAM lParam)
{
	UINT cInputs = LOWORD(wParam);
	PTOUCHINPUT pInputs = new TOUCHINPUT[cInputs];

	if (!pInputs) return;
	if (!GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, pInputs, sizeof(TOUCHINPUT))) return;

	for (UINT i = 0; i < cInputs; i++)
	{
		TOUCHINPUT touch = pInputs[i];

		POINT p;
		p.x = touch.x / 100;
		p.y = touch.y / 100;
		ScreenToClient(window->handle, &p);

		Vector2 position = Vector2(((float)p.x - window->offsetX) * window->scaleX, window->screenHeight - ((float)p.y - window->offsetY) * window->scaleY);
		PointerData data {};

		if ((touch.dwFlags & TOUCHEVENTF_DOWN) != 0)
		{
			msg = WM_POINTERDOWN;
			data.changedButtons = POINTER_CHANGE_FIRSTBUTTON_DOWN;
		}
		else if ((touch.dwFlags & TOUCHEVENTF_UP) != 0)
		{
			msg = WM_POINTERLEAVE;
			data.changedButtons = POINTER_CHANGE_FIRSTBUTTON_UP;
		}
		else if ((touch.dwFlags & TOUCHEVENTF_MOVE) != 0)
		{
			msg = WM_POINTERUPDATE;
		}

		_delegate(window->displayIndex, touch.dwID, msg, PT_TOUCH, position, data);
	}

	CloseTouchInputHandle((HTOUCHINPUT)lParam);
	delete[] pInputs;
}

void log(const wchar_t* str)
{
#if _DEBUG
	BSTR bstr = SysAllocString(str);
	_log(bstr);
	SysFreeString(bstr);
#endif
}

void registerWindow(HWND window, int i)
{
	WindowData *currentWindow = &_windows[i];
	currentWindow->displayIndex = i;
	currentWindow->handle = window;
	
	if (_api == WIN8)
	{
		HINSTANCE h = LoadLibrary(TEXT("user32.dll"));
		GetPointerInfo = (GET_POINTER_INFO)GetProcAddress(h, "GetPointerInfo");
		GetPointerTouchInfo = (GET_POINTER_TOUCH_INFO)GetProcAddress(h, "GetPointerTouchInfo");
		GetPointerPenInfo = (GET_POINTER_PEN_INFO)GetProcAddress(h, "GetPointerPenInfo");

		currentWindow->oldWindowProc = SetWindowLongPtr(currentWindow->handle, GWLP_WNDPROC, (LONG_PTR)wndProc8);
		log(L"Initialized WIN8 input.");
	}
	else
	{
		RegisterTouchWindow(currentWindow->handle, 0);
		currentWindow->oldWindowProc = SetWindowLongPtr(currentWindow->handle, GWLP_WNDPROC, (LONG_PTR)wndProc7);
		log(L"Initialized WIN7 input.");
	}
}

WindowData *lookupWindowByHandle(HWND hwnd)
{
	for (int i = 0; i < MAX_WINDOWS; i++)
	{
		if (_windows[i].handle == hwnd)
		{
			return &_windows[i];
		}
	}

	return NULL;
}

bool isFullscreen(HWND window)
{
	RECT a, b;
	GetWindowRect(window, &a);
	GetWindowRect(GetDesktopWindow(), &b);
	return (a.left == b.left  &&
		a.top == b.top   &&
		a.right == b.right &&
		a.bottom == b.bottom);
}

WindowData *findWindowDataByHandle(HWND handle)
{
	for (int i = 0; i < MAX_WINDOWS; i++)
	{
		WindowData *windowData = &_windows[i];
		if (windowData->handle == handle)
		{
			return windowData;
		}
	}

	return NULL;
}

HWND findNewWindow()
{
	HWND window = NULL;

	while ((window = FindWindowExA(NULL, window, "UnityWndClass", NULL)))
	{
		if (!findWindowDataByHandle(window))
		{
			return window;
		}
	}

	return NULL;
}