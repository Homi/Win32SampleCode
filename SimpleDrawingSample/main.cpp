#include <Windows.h>
#include <WindowsX.h>
#include <d2d1.h>

#pragma comment(lib, "d2d1")

#include <stdlib.h>
#include <time.h>
#include <list>
#include <memory>

using namespace std;

#include "basewin.h"
#include "resource.h"

const TCHAR InfoText[] = 
	L"The program has three modes:\n"
	L"\n"
	L"Draw mode.	The user can draw new ellipses.\n"
	L"Selection mode.	The user can select an ellipse.\n"
	L"Drag mode.	The user can drag a selected ellipse.\n"
	L"\n"
	L"Command Keys\n"
	L"CTRL+M	Toggle between modes.\n"
	L"F1	Switch to draw mode.\n"
	L"F2	Switch to selection mode.";

template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

// Ellipse
struct MyEllipse
{
    D2D1_ELLIPSE    ellipse;
    D2D1_COLOR_F    color;

    void Draw(ID2D1RenderTarget *pRT, ID2D1SolidColorBrush *pBrush)
    {
        pBrush->SetColor(color);
        pRT->FillEllipse(ellipse, pBrush);
        pBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
        pRT->DrawEllipse(ellipse, pBrush, 1.0f);
    }

    BOOL HitTest(float x, float y)
    {
        const float a = ellipse.radiusX;
        const float b = ellipse.radiusY;
        const float x1 = x - ellipse.point.x;
        const float y1 = y - ellipse.point.y;
        const float d = ((x1 * x1) / (a * a)) + ((y1 * y1) / (b * b));
        return d <= 1.0f;
    }
};

/*
 * Mouse coordinates are given in physical pixels, but Direct2D expects 
 * device-independent pixels (DIPs). To handle high-DPI settings correctly, 
 * you must translate the pixel coordinates into DIPs.
 */

class DPIScale
{
    static float scaleX;
    static float scaleY;

public:
    static void Initialize(ID2D1Factory *pFactory)
    {
        FLOAT dpiX, dpiY;
        pFactory->GetDesktopDpi(&dpiX, &dpiY);
        scaleX = dpiX/96.0f;
        scaleY = dpiY/96.0f;
    }

    template <typename T>
    static D2D1_POINT_2F PixelsToDips(T x, T y)
    {
        return D2D1::Point2F(static_cast<float>(x) / scaleX, static_cast<float>(y) / scaleY);
    }

	template <typename T>
	static float PixelsToDipsX(T x)
	{
		return static_cast<float>(x) / scaleX;
	}
	
	template <typename T>
	static float PixelsToDipsY(T y)
	{
		return static_cast<float>(y) / scaleY;
	}
};

float DPIScale::scaleX = 1.0f;
float DPIScale::scaleY = 1.0f;

class RandomRGB {
public:
	static COLORREF get(){
		return RGB(rand()%256,rand()%256,rand()%256);
	}
};

enum Mode {DrawMode, SelectMode, DragMode};

class MainWindow : public BaseWindow<MainWindow>
{
    ID2D1Factory            *pFactory;
    ID2D1HwndRenderTarget   *pRenderTarget;
    ID2D1SolidColorBrush    *pBrush;
	D2D1_POINT_2F           ptMouse;
	Mode					mode;

	HCURSOR					hCursor;
	RandomRGB				rgb;
	
	list<shared_ptr<MyEllipse>>             ellipses;
	list<shared_ptr<MyEllipse>>::iterator   selection;
     
    shared_ptr<MyEllipse> Selection() 
    { 
        if (selection == ellipses.end()) 
        { 
            return nullptr;
        }
        else
        {
            return (*selection);
        }
    }

    void ClearSelection() { selection = ellipses.end(); }

	void SetMode(Mode m);
	void InsertEllipse(float x, float y);
	BOOL HitTest(float x, float y);

	void    CalculateLayout(){}
    HRESULT CreateGraphicsResources();
    void    DiscardGraphicsResources();

    void    OnPaint();
    void    Resize();
	void    OnLButtonDown(int pixelX, int pixelY, DWORD flags);
    void    OnLButtonUp();
    void    OnMouseMove(int pixelX, int pixelY, DWORD flags);

public:
	MainWindow() : pFactory(NULL), pRenderTarget(NULL), pBrush(NULL),
		ptMouse(D2D1::Point2F()), mode(DrawMode)
    {
    }

    PCWSTR  ClassName() const { return L"Circle Window Class"; }
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT HandleCommand(WPARAM wParam, LPARAM lParam);
};


HRESULT MainWindow::CreateGraphicsResources()
{
    HRESULT hr = S_OK;
    if (pRenderTarget == NULL)
    {
        RECT rc;
        GetClientRect(m_hwnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

        hr = pFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd, size),
            &pRenderTarget);

        if (SUCCEEDED(hr))
        {
            const D2D1_COLOR_F color = D2D1::ColorF(1.0f, 1.0f, 0);
            hr = pRenderTarget->CreateSolidColorBrush(color, &pBrush);

            if (SUCCEEDED(hr))
            {
                CalculateLayout();
            }
        }
    }
    return hr;
}

void MainWindow::DiscardGraphicsResources()
{
    SafeRelease(&pRenderTarget);
    SafeRelease(&pBrush);
}

void MainWindow::OnPaint()
{
    HRESULT hr = CreateGraphicsResources();
    if (SUCCEEDED(hr))
    {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
     
        pRenderTarget->BeginDraw();

        pRenderTarget->Clear( D2D1::ColorF(D2D1::ColorF::SkyBlue) );
		SetMode(mode);
		for(list<shared_ptr<MyEllipse>>::reverse_iterator it=ellipses.rbegin();
			it != ellipses.rend(); ++it)
			(*it)->Draw(pRenderTarget, pBrush);

        hr = pRenderTarget->EndDraw();

        if (FAILED(hr) || hr == D2DERR_RECREATE_TARGET)
        {
            DiscardGraphicsResources();
        }
        EndPaint(m_hwnd, &ps);
    }
}

void MainWindow::Resize()
{
    if (pRenderTarget != NULL)
    {
        RECT rc;
        GetClientRect(m_hwnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

        pRenderTarget->Resize(size);
        CalculateLayout();
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

// Left Button Down
void MainWindow::OnLButtonDown(int pixelX, int pixelY, DWORD flags)
{
    const float dipX = DPIScale::PixelsToDipsX(pixelX);
    const float dipY = DPIScale::PixelsToDipsY(pixelY);

	// Draw Mode
    if (mode == DrawMode)
    {
        POINT pt = { pixelX, pixelY };

        if (DragDetect(m_hwnd, pt))
        {
            SetCapture(m_hwnd);
        
            // Start a new ellipse.
            InsertEllipse(dipX, dipY);
        }
    }
	// Selection Mode
    else 
    {
        ClearSelection();

        if (HitTest(dipX, dipY))
        {
            SetCapture(m_hwnd);

            ptMouse = Selection()->ellipse.point;
            ptMouse.x -= dipX;
            ptMouse.y -= dipY;

            SetMode(DragMode);
        }
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

// Mouse Move
void MainWindow::OnMouseMove(int pixelX, int pixelY, DWORD flags)
{
    const float dipX = DPIScale::PixelsToDipsX(pixelX);
    const float dipY = DPIScale::PixelsToDipsY(pixelY);

    if ((flags & MK_LBUTTON) && Selection())
    { 
        if (mode == DrawMode)
        {
            // Resize the ellipse.
            const float width = (dipX - ptMouse.x) / 2;
            const float height = (dipY - ptMouse.y) / 2;
            const float x1 = ptMouse.x + width;
            const float y1 = ptMouse.y + height;

            Selection()->ellipse = D2D1::Ellipse(D2D1::Point2F(x1, y1), width, height);
        }
        else if (mode == DragMode)
        {
            // Move the ellipse.
            Selection()->ellipse.point.x = dipX + ptMouse.x;
            Selection()->ellipse.point.y = dipY + ptMouse.y;
        }
		// Makes sure that the window is repainted
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

// Left Button Up
void MainWindow::OnLButtonUp()
{
    if ((mode == DrawMode) && Selection())
    {
        ClearSelection();
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
    else if (mode == DragMode)
    {
        SetMode(SelectMode);
    }
    ReleaseCapture(); 
}

void MainWindow::SetMode(Mode m)
{
    mode = m;

    // Update the cursor
    LPWSTR cursor;
    switch (mode)
    {
    case DrawMode:
        cursor = IDC_CROSS;
        break;

    case SelectMode:
        cursor = IDC_HAND;
        break;

    case DragMode:
        cursor = IDC_SIZEALL;
        break;
    }

    hCursor = LoadCursor(NULL, cursor);
    SetCursor(hCursor);
}

void MainWindow::InsertEllipse(float x, float y)
{
	ptMouse = D2D1::Point2F(x, y);
	ellipses.push_front(make_shared<MyEllipse>());
	selection = ellipses.begin();
	Selection()->ellipse = D2D1::Ellipse(ptMouse, 1.0f, 1.0f);
	Selection()->color = D2D1::ColorF(rgb.get());
}

BOOL MainWindow::HitTest(float x, float y)
{
	list<shared_ptr<MyEllipse>>::iterator it = ellipses.begin();
	for(; it != ellipses.end(); ++it)
	{
		if((*it)->HitTest(x, y))
		{
			selection = it;
			return TRUE;
		}
	}
	return FALSE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	srand ((unsigned)time(NULL));

    MainWindow win;

    if (!win.Create(L"Circle", WS_OVERLAPPEDWINDOW))
    {
        return 0;
    }

    ShowWindow(win.Window(), nCmdShow);

	MessageBox(win.Window(), InfoText, TEXT("Draw Circle"), MB_OK);

    // Load an accelerator table
	HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR1));

	// Run the message loop.
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (!TranslateAccelerator(win.Window(), hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        if (FAILED(D2D1CreateFactory(
			D2D1_FACTORY_TYPE_SINGLE_THREADED, &pFactory)))
        {
            return -1;  // Fail CreateWindowEx.
        }
		DPIScale::Initialize(pFactory);
        return 0;

    case WM_DESTROY:
        DiscardGraphicsResources();
        SafeRelease(&pFactory);
        PostQuitMessage(0);
        return 0;

    case WM_PAINT:
        OnPaint();
        return 0;
	case WM_SIZE:
        Resize();
        return 0;

	case WM_LBUTTONDOWN: 
        OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (DWORD)wParam);
        return 0;

    case WM_LBUTTONUP: 
        OnLButtonUp();
        return 0;

    case WM_MOUSEMOVE: 
        OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (DWORD)wParam);
        return 0;
	case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) //the cursor is over the client area of the window
        {
            SetCursor(hCursor);
            return TRUE;
        }
        break;
	case WM_COMMAND:
		return HandleCommand(wParam, lParam);
        
    }

    return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
}

LRESULT MainWindow::HandleCommand(WPARAM wParam, LPARAM lParam)
{
	switch (LOWORD(wParam))
	{
	case ID_DRAW_MODE:
		SetMode(DrawMode);
		break;

	case ID_SELECT_MODE:
		SetMode(SelectMode);
		break;

	case ID_TOGGLE_MODE:
		if (mode == DrawMode)
		{
			SetMode(SelectMode);
		}
		else
		{
			SetMode(DrawMode);
		}
		break;
	}
	return 0;
}