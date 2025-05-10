#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <random>
#include <ctime>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

// Add using namespace for GDI+ at the top
using namespace Gdiplus;

// Application constants
const int TIMER_ID = 1;
const int MIN_STATE_DURATION = 5000;  // 5 seconds in milliseconds
const int MAX_STATE_DURATION = 20000; // 20 seconds in milliseconds
const int ANIMATION_TIMER_ID = 2;
const int ANIMATION_INTERVAL = 16;  // 16ms for 60 FPS
const int FRAME_BUFFER_SIZE = 5;  // Number of frames to buffer ahead
const UINT MIN_FRAME_DELAY = 16;   // Minimum frame delay (60 FPS)
const int MOVE_INTERVAL = 16;  // Changed to 16ms for smoother movement
const int MOVE_DISTANCE = 2;   // Reduced movement distance per step

// GIF categories
enum GifType {
    MOVE,
    WAIT,
    SIT,
    PICK,
    MISC
};

// Application modes
enum AppMode {
    AUTOMATIC,
    MANUAL
};

// Application states
enum AppState {
    STATE_MOVE,
    STATE_WAIT,
    STATE_SIT,
    STATE_PICK,
    STATE_MISC
};

// Structure to store GIF information
struct GifAnimation {
    Gdiplus::Image* image;
    UINT frameCount;
    UINT currentFrame;
    std::vector<UINT> frameDelays;
    bool isPlaying;
    std::unique_ptr<Gdiplus::Bitmap> backBuffer;

    // Default constructor
    GifAnimation() : image(nullptr), frameCount(0), currentFrame(0), isPlaying(false) {}

    // Move constructor
    GifAnimation(GifAnimation&& other) noexcept
        : image(other.image),
          frameCount(other.frameCount),
          currentFrame(other.currentFrame),
          frameDelays(std::move(other.frameDelays)),
          isPlaying(other.isPlaying),
          backBuffer(std::move(other.backBuffer)) {
        other.image = nullptr;
    }

    // Move assignment operator
    GifAnimation& operator=(GifAnimation&& other) noexcept {
        if (this != &other) {
            if (image) delete image;
            image = other.image;
            frameCount = other.frameCount;
            currentFrame = other.currentFrame;
            frameDelays = std::move(other.frameDelays);
            isPlaying = other.isPlaying;
            backBuffer = std::move(other.backBuffer);
            other.image = nullptr;
        }
        return *this;
    }

    // Destructor
    ~GifAnimation() {
        if (image) {
            delete image;
            image = nullptr;
        }
    }
};

struct GifInfo {
    std::wstring filePath;
    GifType type;
    GifAnimation animation;
    bool flipped;

    // Default constructor
    GifInfo() : type(MISC), flipped(false) {}

    // Move constructor
    GifInfo(GifInfo&& other) noexcept
        : filePath(std::move(other.filePath)),
          type(other.type),
          animation(std::move(other.animation)),
          flipped(other.flipped) {}

    // Move assignment operator
    GifInfo& operator=(GifInfo&& other) noexcept {
        if (this != &other) {
            filePath = std::move(other.filePath);
            type = other.type;
            animation = std::move(other.animation);
            flipped = other.flipped;
        }
        return *this;
    }
};

// Add new structures for frame queueing
struct FrameInfo {
    Gdiplus::Image* image;
    UINT frameIndex;
    UINT delay;
    bool flipped;
};

// Global variables
HWND g_hwnd = NULL;
std::vector<GifInfo> g_gifs;
size_t g_currentGifIndex = 0;
AppMode g_appMode = AUTOMATIC;
AppState g_appState = STATE_WAIT;
AppState g_prevState = STATE_WAIT;
bool g_isPickMode = false;
bool g_moveDirectionRight = true;
bool g_menuVisible = false;
HWND g_importButton = NULL;
HWND g_quitButton = NULL;
int g_miscGifIndex = 0;
std::mt19937 g_randomEngine(static_cast<unsigned int>(time(nullptr)));
HWND g_startupText = NULL;
bool g_hasGifs = false;
const int MENU_WIDTH = 300;  // Reduced size since we only have buttons
const int MENU_HEIGHT = 150; // Reduced height
const int BUTTON_WIDTH = 250;
const int BUTTON_HEIGHT = 40;
const int BUTTON_MARGIN = 20;
const int TEXT_MARGIN = 30;

// Add global variables for frame queueing
std::vector<FrameInfo> g_frameQueue;
size_t g_currentFrameIndex = 0;
bool g_isQueueingFrames = false;

// Add new global variables for menu window
HWND g_menuHwnd = NULL;
const wchar_t MENU_CLASS_NAME[] = L"ChibiViewerMenuClass";

// Add new global variable for frame timing
LARGE_INTEGER g_performanceFrequency;
LARGE_INTEGER g_lastFrameTime;
double g_frameTime = 0.0;

// Add new global variables for frame management
bool g_isRendering = false;
Gdiplus::Image* g_currentRenderedFrame = nullptr;

// Add at the top of the file with other global variables
bool needsClear = true;

// Add at the top with other global variables
Gdiplus::Bitmap* g_topLayer = nullptr;
Gdiplus::Bitmap* g_bottomLayer = nullptr;

// Function prototypes
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void RenderGif(HWND hwnd);
bool LoadGifsFromFolder(const std::wstring& folderPath);
void SwitchToNextGif();
void UpdateAppState();
void StartStateTimer();
void MoveWindow();
void ToggleMenu();
void CreateButtons(HWND hwnd);
GifType GetGifTypeFromFilename(const std::wstring& filename);
void CleanupGifs();
void QueueFramesFromGif(size_t gifIndex);

// Add new helper functions
std::vector<UINT> LoadGifFrameInfo(Gdiplus::Image* image) {
    UINT count = image->GetFrameDimensionsCount();
    if (count != 1) {
        return std::vector<UINT>();
    }

    GUID guid;
    if (image->GetFrameDimensionsList(&guid, 1) != 0) {
        return std::vector<UINT>();
    }
    UINT frameCount = image->GetFrameCount(&guid);

    UINT sz = image->GetPropertyItemSize(PropertyTagFrameDelay);
    if (sz == 0) {
        return std::vector<UINT>();
    }

    std::vector<BYTE> buffer(sz);
    Gdiplus::PropertyItem* propertyItem = reinterpret_cast<Gdiplus::PropertyItem*>(&buffer[0]);
    image->GetPropertyItem(PropertyTagFrameDelay, sz, propertyItem);
    UINT* frameDelayArray = (UINT*)propertyItem->value;

    std::vector<UINT> frameDelays(frameCount);
    std::transform(frameDelayArray, frameDelayArray + frameCount, frameDelays.begin(),
        [](UINT n) { return n * 10; });

    return frameDelays;
}

void GenerateFrame(Gdiplus::Bitmap* bmp, Gdiplus::Image* gif) {
    Gdiplus::Graphics dest(bmp);
    
    // Clear with black (transparent color)
    Gdiplus::SolidBrush black(Gdiplus::Color::Black);
    dest.FillRectangle(&black, 0, 0, bmp->GetWidth(), bmp->GetHeight());
    
    if (gif) {
        // Draw the GIF
        dest.DrawImage(gif, 0, 0);
    }
}

std::unique_ptr<Gdiplus::Bitmap> CreateBackBuffer(HWND hwnd) {
    RECT r;
    GetClientRect(hwnd, &r);
    return std::make_unique<Gdiplus::Bitmap>(r.right - r.left, r.bottom - r.top);
}

// Modify MenuWindowProc to create opaque grey buttons
LRESULT CALLBACK MenuWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HFONT hFont = NULL;  // Make font static to avoid case label jump
    
    switch (uMsg) {
        case WM_CREATE:
            // Create font if not already created
            if (hFont == NULL) {
                hFont = CreateFontW(
                    18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
                );
            }
            
            // Create buttons in the menu window with solid colors
            g_importButton = CreateWindowW(
                L"BUTTON", L"Select GIF Folder",
                WS_CHILD | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER | BS_OWNERDRAW,
                (MENU_WIDTH - BUTTON_WIDTH) / 2, 
                BUTTON_MARGIN,
                BUTTON_WIDTH, BUTTON_HEIGHT,
                hwnd, NULL, GetModuleHandle(NULL), NULL
            );
            
            g_quitButton = CreateWindowW(
                L"BUTTON", L"Exit Program",
                WS_CHILD | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER | BS_OWNERDRAW,
                (MENU_WIDTH - BUTTON_WIDTH) / 2, 
                BUTTON_MARGIN * 2 + BUTTON_HEIGHT,
                BUTTON_WIDTH, BUTTON_HEIGHT,
                hwnd, NULL, GetModuleHandle(NULL), NULL
            );
            
            // Set font for all controls
            SendMessage(g_importButton, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_quitButton, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Show buttons
            ShowWindow(g_importButton, SW_SHOW);
            ShowWindow(g_quitButton, SW_SHOW);
            return 0;
            
        case WM_DRAWITEM:
            if (wParam == 0 || wParam == 1) {  // Button IDs
                LPDRAWITEMSTRUCT lpDrawItem = (LPDRAWITEMSTRUCT)lParam;
                
                // Set button colors
                HBRUSH hBrush;
                if (lpDrawItem->itemState & ODS_SELECTED) {
                    hBrush = CreateSolidBrush(RGB(180, 180, 180));  // Darker grey when pressed
                } else {
                    hBrush = CreateSolidBrush(RGB(220, 220, 220));  // Light grey normal state
                }
                
                // Fill button background
                FillRect(lpDrawItem->hDC, &lpDrawItem->rcItem, hBrush);
                DeleteObject(hBrush);
                
                // Draw button border
                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
                HPEN hOldPen = (HPEN)SelectObject(lpDrawItem->hDC, hPen);
                Rectangle(lpDrawItem->hDC, 
                         lpDrawItem->rcItem.left, lpDrawItem->rcItem.top,
                         lpDrawItem->rcItem.right, lpDrawItem->rcItem.bottom);
                SelectObject(lpDrawItem->hDC, hOldPen);
                DeleteObject(hPen);
                
                // Draw button text
                SetBkMode(lpDrawItem->hDC, TRANSPARENT);
                SetTextColor(lpDrawItem->hDC, RGB(0, 0, 0));  // Black text
                
                // Get button text
                wchar_t buttonText[256];
                GetWindowTextW(lpDrawItem->hwndItem, buttonText, 256);
                
                // Calculate text position
                RECT textRect = lpDrawItem->rcItem;
                textRect.left += 10;
                textRect.right -= 10;
                
                // Draw text
                DrawTextW(lpDrawItem->hDC, buttonText, -1, &textRect,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                
                return TRUE;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
            
        case WM_DESTROY:
            // Clean up font
            if (hFont != NULL) {
                DeleteObject(hFont);
                hFont = NULL;
            }
            return 0;
            
        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED) {
                if ((HWND)lParam == g_quitButton) {
                    DestroyWindow(g_hwnd);  // Close main window
                } else if ((HWND)lParam == g_importButton) {
                    BROWSEINFOW bi = {0};
                    bi.hwndOwner = hwnd;
                    bi.lpszTitle = L"Select Folder with GIF Files";
                    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                    
                    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
                    if (pidl != NULL) {
                        wchar_t folderPath[MAX_PATH] = {0};
                        if (SHGetPathFromIDListW(pidl, folderPath)) {
                            // Clean up existing GIFs
                            CleanupGifs();
                            
                            // Load new GIFs
                            if (LoadGifsFromFolder(folderPath)) {
                                // Reset to initial state
                                g_currentGifIndex = 0;
                                g_appState = STATE_WAIT;
                                InvalidateRect(g_hwnd, NULL, TRUE);
                                
                                if (g_appMode == AUTOMATIC) {
                                    StartStateTimer();
                                }
                            }
                        }
                        CoTaskMemFree(pidl);
                    }
                    ToggleMenu();
                }
            }
            return 0;
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Draw menu background (transparent)
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            HBRUSH menuBrush = CreateSolidBrush(RGB(240, 240, 240));
            FillRect(hdc, &clientRect, menuBrush);
            DeleteObject(menuBrush);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Add new function to get program directory
std::wstring GetProgramDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return std::wstring(path);
}

// Main entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize performance counter
    QueryPerformanceFrequency(&g_performanceFrequency);
    QueryPerformanceCounter(&g_lastFrameTime);
    
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // Register the main window class
    const wchar_t CLASS_NAME[] = L"ChibiViewerWindowClass";
    
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    RegisterClassW(&wc);
    
    // Register the menu window class
    WNDCLASSW menuWc = {};
    menuWc.lpfnWndProc = MenuWindowProc;
    menuWc.hInstance = hInstance;
    menuWc.lpszClassName = MENU_CLASS_NAME;
    menuWc.hCursor = LoadCursor(NULL, IDC_ARROW);
    menuWc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&menuWc);

    // Create the main window
    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST,
        CLASS_NAME,
        L"Chibi Viewer",
        WS_POPUP,
        100, 100, 200, 200,
        NULL, NULL, hInstance, NULL
    );

    if (g_hwnd == NULL) {
        return 0;
    }

    // Create the menu window
    g_menuHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST,
        MENU_CLASS_NAME,
        L"Chibi Viewer Menu",
        WS_POPUP,
        100, 100, MENU_WIDTH, MENU_HEIGHT,
        g_hwnd, NULL, hInstance, NULL
    );

    if (g_menuHwnd == NULL) {
        DestroyWindow(g_hwnd);
        return 0;
    }

    // Set window transparency
    SetLayeredWindowAttributes(g_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    SetLayeredWindowAttributes(g_menuHwnd, RGB(240, 240, 240), 0, LWA_COLORKEY);
    
    // Show the windows
    ShowWindow(g_hwnd, nCmdShow);
    ShowWindow(g_menuHwnd, SW_HIDE);  // Menu starts hidden

    // Try to load GIFs from program directory first
    std::wstring programDir = GetProgramDirectory();
    if (!LoadGifsFromFolder(programDir)) {
        // If no GIFs found in program directory, show menu
        ToggleMenu();
    } else {
        // If GIFs were loaded, start with automatic mode
        g_appMode = AUTOMATIC;
        StartStateTimer();
    }

    // Main message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    CleanupGifs();
    
    // Shutdown GDI+
    Gdiplus::GdiplusShutdown(gdiplusToken);

    return 0;
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_ID) {
                if (g_appState == STATE_MOVE) {
                    MoveWindow();
                } else {
                    UpdateAppState();
                }
            } else if (wParam == ANIMATION_TIMER_ID && !g_frameQueue.empty()) {
                // Calculate frame time
                LARGE_INTEGER currentTime;
                QueryPerformanceCounter(&currentTime);
                double deltaTime = (currentTime.QuadPart - g_lastFrameTime.QuadPart) * 1000.0 / g_performanceFrequency.QuadPart;
                g_lastFrameTime = currentTime;
                
                // Move to next frame in queue
                g_currentFrameIndex = (g_currentFrameIndex + 1) % g_frameQueue.size();
                
                // Set timer for next frame
                UINT nextDelay = std::max(g_frameQueue[g_currentFrameIndex].delay, MIN_FRAME_DELAY);
                SetTimer(hwnd, ANIMATION_TIMER_ID, nextDelay, NULL);
                
                // Force redraw
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;

        case WM_PAINT: {
            if (g_isRendering) return 0;  // Prevent re-entrant rendering
            g_isRendering = true;
            
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Only clear if necessary (when switching GIFs or states)
            if (needsClear) {
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(hdc, &clientRect, hBrush);
                DeleteObject(hBrush);
                needsClear = false;
                
                // Initialize layers if needed
                if (!g_topLayer || !g_bottomLayer) {
                    if (g_topLayer) delete g_topLayer;
                    if (g_bottomLayer) delete g_bottomLayer;
                    
                    RECT rect;
                    GetClientRect(hwnd, &rect);
                    g_topLayer = new Gdiplus::Bitmap(rect.right, rect.bottom);
                    g_bottomLayer = new Gdiplus::Bitmap(rect.right, rect.bottom);
                }
            }
            
            // Draw the current frame from the queue
            if (!g_frameQueue.empty() && g_currentFrameIndex < g_frameQueue.size()) {
                FrameInfo& frame = g_frameQueue[g_currentFrameIndex];
                
                // Select the correct frame
                GUID timeDimension = Gdiplus::FrameDimensionTime;
                frame.image->SelectActiveFrame(&timeDimension, frame.frameIndex);
                
                // Draw new frame to top layer
                Gdiplus::Graphics topGraphics(g_topLayer);
                topGraphics.Clear(Gdiplus::Color::Black);
                topGraphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQuality);
                topGraphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
                
                // Get image dimensions
                int width = frame.image->GetWidth();
                int height = frame.image->GetHeight();
                
                // Apply horizontal flip if needed
                if (frame.flipped) {
                    // Create a temporary bitmap for the flipped image
                    Gdiplus::Bitmap flippedBitmap(width, height);
                    Gdiplus::Graphics flippedGraphics(&flippedBitmap);
                    
                    // Draw the original image to the temporary bitmap
                    flippedGraphics.DrawImage(frame.image, 0, 0, width, height);
                    
                    // Draw the flipped image to the top layer
                    topGraphics.DrawImage(&flippedBitmap, 0, 0, width, height);
                } else {
                    // Draw the original image
                    topGraphics.DrawImage(frame.image, 0, 0, width, height);
                }
                
                // Draw to screen
                Gdiplus::Graphics screenGraphics(hdc);
                screenGraphics.DrawImage(g_bottomLayer, 0, 0);
                screenGraphics.DrawImage(g_topLayer, 0, 0);
                
                // Move top layer to bottom layer for next frame
                Gdiplus::Graphics bottomGraphics(g_bottomLayer);
                bottomGraphics.Clear(Gdiplus::Color::Black);
                bottomGraphics.DrawImage(g_topLayer, 0, 0);
            }
            
            EndPaint(hwnd, &ps);
            g_isRendering = false;
            return 0;
        }

        case WM_SIZE: {
            // Recreate back buffers for all GIFs
            for (auto& gif : g_gifs) {
                gif.animation.backBuffer = CreateBackBuffer(hwnd);
                GenerateFrame(gif.animation.backBuffer.get(), gif.animation.image);
            }
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        case WM_KEYDOWN:
            switch (wParam) {
                case 'M':
                    ToggleMenu();
                    break;
                    
                case 'A':
                    g_appMode = (g_appMode == AUTOMATIC) ? MANUAL : AUTOMATIC;
                    if (g_appMode == AUTOMATIC) {
                        StartStateTimer();
                    } else {
                        KillTimer(hwnd, TIMER_ID);
                    }
                    break;
                    
                case VK_SPACE:
                    if (g_appMode == MANUAL && !g_gifs.empty()) {
                        SwitchToNextGif();
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                    break;
            }
            return 0;
            
        case WM_MOUSEMOVE:
            if (g_isPickMode) {
                // Move the window with the mouse
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ClientToScreen(hwnd, &pt);
                
                RECT windowRect;
                GetWindowRect(hwnd, &windowRect);
                int width = windowRect.right - windowRect.left;
                int height = windowRect.bottom - windowRect.top;
                
                // Ensure the window stays within screen bounds
                int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);
                
                // Calculate new position
                int newX = pt.x - width / 2;
                int newY = pt.y - height / 2;
                
                // Clamp to screen bounds
                newX = std::max(0, std::min(newX, screenWidth - width));
                newY = std::max(0, std::min(newY, screenHeight - height));
                
                SetWindowPos(hwnd, NULL, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            return 0;
            
        case WM_LBUTTONDOWN:
            if (!g_gifs.empty()) {
                g_prevState = g_appState;
                g_isPickMode = true;
                g_appState = STATE_PICK;
                
                // Queue frames from the PICK GIF
                for (size_t i = 0; i < g_gifs.size(); i++) {
                    if (g_gifs[i].type == PICK) {
                        // Clear existing queue
                        g_frameQueue.clear();
                        g_currentFrameIndex = 0;
                        
                        // Queue frames from the PICK GIF
                        QueueFramesFromGif(i);
                        
                        // Start animation timer
                        if (!g_frameQueue.empty()) {
                            SetTimer(hwnd, ANIMATION_TIMER_ID, g_frameQueue[0].delay, NULL);
                        }
                        break;
                    }
                }
                
                InvalidateRect(hwnd, NULL, TRUE);
                SetCapture(hwnd);
            }
            return 0;
            
        case WM_LBUTTONUP:
            if (g_isPickMode) {
                g_isPickMode = false;
                g_appState = g_prevState;
                
                // Queue frames from the previous state GIF
                size_t newGifIndex = g_currentGifIndex;
                bool foundGif = false;
                
                GifType targetType;
                switch (g_prevState) {
                    case STATE_MOVE: targetType = MOVE; break;
                    case STATE_WAIT: targetType = WAIT; break;
                    case STATE_SIT: targetType = SIT; break;
                    default: targetType = MISC; break;
                }
                
                for (size_t i = 0; i < g_gifs.size(); i++) {
                    if (g_gifs[i].type == targetType) {
                        newGifIndex = i;
                        foundGif = true;
                        break;
                    }
                }
                
                if (foundGif && newGifIndex < g_gifs.size()) {
                    // Clear existing queue
                    g_frameQueue.clear();
                    g_currentFrameIndex = 0;
                    
                    // Queue frames from the new GIF
                    QueueFramesFromGif(newGifIndex);
                    
                    // Start animation timer
                    if (!g_frameQueue.empty()) {
                        SetTimer(hwnd, ANIMATION_TIMER_ID, g_frameQueue[0].delay, NULL);
                    }
                }
                
                InvalidateRect(hwnd, NULL, TRUE);
                ReleaseCapture();
                
                if (g_appMode == AUTOMATIC) {
                    StartStateTimer();
                }
            }
            return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Render the current GIF
void RenderGif(HWND hwnd) {
    if (g_gifs.empty()) {
        return;
    }
    
    size_t gifIndex = g_currentGifIndex;
    
    // If in pick mode, find and use the PICK gif
    if (g_appState == STATE_PICK) {
        for (size_t i = 0; i < g_gifs.size(); i++) {
            if (g_gifs[i].type == PICK) {
                gifIndex = i;
                break;
            }
        }
    } else {
        // Otherwise, find a gif of the appropriate type for the current state
        GifType targetType;
        switch (g_appState) {
            case STATE_MOVE: targetType = MOVE; break;
            case STATE_WAIT: targetType = WAIT; break;
            case STATE_SIT: targetType = SIT; break;
            default: targetType = MISC; break;
        }
        
        for (size_t i = 0; i < g_gifs.size(); i++) {
            if (g_gifs[i].type == targetType) {
                gifIndex = i;
                break;
            }
        }
    }
    
    if (gifIndex < g_gifs.size() && g_gifs[gifIndex].animation.image != nullptr) {
        // Update the back buffer
        GenerateFrame(g_gifs[gifIndex].animation.backBuffer.get(), g_gifs[gifIndex].animation.image);
    }
}

// Modify ResizeWindowToGif to reduce unnecessary updates
void ResizeWindowToGif(HWND hwnd, Gdiplus::Image* gif) {
    if (!gif) return;
    
    // Get current window position
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    
    // Get GIF dimensions
    int gifWidth = gif->GetWidth();
    int gifHeight = gif->GetHeight();
    
    // Only resize if dimensions have changed
    if (windowRect.right - windowRect.left != gifWidth || 
        windowRect.bottom - windowRect.top != gifHeight) {
        // Resize window to fit GIF without forcing redraw
        SetWindowPos(hwnd, NULL, windowRect.left, windowRect.top, 
                    gifWidth, gifHeight, 
                    SWP_NOZORDER | SWP_NOREDRAW);
    }
}

// Modify QueueFramesFromGif to properly handle flipped state
void QueueFramesFromGif(size_t gifIndex) {
    if (gifIndex >= g_gifs.size()) return;
    
    GifInfo& gif = g_gifs[gifIndex];
    GUID timeDimension = Gdiplus::FrameDimensionTime;
    
    // Clear existing queue
    g_frameQueue.clear();
    g_currentFrameIndex = 0;
    
    // Queue all frames from the GIF multiple times to create a larger buffer
    for (int bufferPass = 0; bufferPass < FRAME_BUFFER_SIZE; bufferPass++) {
        for (UINT i = 0; i < gif.animation.frameCount; i++) {
            FrameInfo frame;
            frame.image = gif.animation.image;
            frame.frameIndex = i;
            frame.delay = std::max(gif.animation.frameDelays[i], MIN_FRAME_DELAY);
            frame.flipped = gif.flipped;  // Set the flipped state from the GIF
            g_frameQueue.push_back(frame);
        }
    }
    
    // Reset timing
    QueryPerformanceCounter(&g_lastFrameTime);
}

// Modify SwitchToNextGif to set needsClear
void SwitchToNextGif() {
    if (g_gifs.empty()) return;
    
    // Store previous state
    AppState prevState = g_appState;
    
    // Cycle through states
    switch (g_appState) {
        case STATE_MOVE: g_appState = STATE_WAIT; break;
        case STATE_WAIT: g_appState = STATE_SIT; break;
        case STATE_SIT: g_appState = STATE_MISC; break;
        case STATE_MISC: g_appState = STATE_MOVE; break;
        default: g_appState = STATE_WAIT; break;
    }
    
    // Find appropriate GIF for new state
    size_t newGifIndex = g_currentGifIndex;
    bool foundGif = false;
    
    GifType targetType;
    switch (g_appState) {
        case STATE_MOVE: targetType = MOVE; break;
        case STATE_WAIT: targetType = WAIT; break;
        case STATE_SIT: targetType = SIT; break;
        default: targetType = MISC; break;
    }
    
    for (size_t i = 0; i < g_gifs.size(); i++) {
        if (g_gifs[i].type == targetType) {
            newGifIndex = i;
            foundGif = true;
            break;
        }
    }
    
    if (foundGif && newGifIndex < g_gifs.size()) {
        // Clear existing queue
        g_frameQueue.clear();
        g_currentFrameIndex = 0;
        
        // Queue frames from the new GIF
        QueueFramesFromGif(newGifIndex);
        
        // Resize window to fit new GIF
        ResizeWindowToGif(g_hwnd, g_gifs[newGifIndex].animation.image);
        
        // Set needsClear flag
        needsClear = true;
        
        // Start animation timer
        if (!g_frameQueue.empty()) {
            SetTimer(g_hwnd, ANIMATION_TIMER_ID, g_frameQueue[0].delay, NULL);
        }
    } else {
        // If no valid GIF found, revert to previous state
        g_appState = prevState;
    }
}

// Modify UpdateAppState to properly handle move state
void UpdateAppState() {
    if (g_gifs.empty()) {
        return;
    }
    
    // Store current state
    AppState prevState = g_appState;
    
    // Force move state for testing
    int nextState = 0;  // Always use move state
    
    // Move the dirDist declaration outside the switch
    std::uniform_int_distribution<int> dirDist(0, 1);
    
    // Kill existing timers before state transition
    KillTimer(g_hwnd, TIMER_ID);
    KillTimer(g_hwnd, ANIMATION_TIMER_ID);
    
    switch (nextState) {
        case 0:
            g_appState = STATE_MOVE;
            
            // Randomly decide movement direction
            g_moveDirectionRight = dirDist(g_randomEngine) == 1;
            
            // Set the flipped state of MOVE gifs based on direction
            for (size_t i = 0; i < g_gifs.size(); i++) {
                if (g_gifs[i].type == MOVE) {
                    g_gifs[i].flipped = !g_moveDirectionRight;
                }
            }
            
            // Start moving the window with consistent timing
            SetTimer(g_hwnd, TIMER_ID, MOVE_INTERVAL, NULL);
            break;
    }
    
    // Find the appropriate GIF for the new state 
    size_t newGifIndex = g_currentGifIndex;
    bool foundGif = false;
    
    GifType targetType;
    switch (g_appState) {
        case STATE_MOVE: targetType = MOVE; break;
    }
    
    for (size_t i = 0; i < g_gifs.size(); i++) {
        if (g_gifs[i].type == targetType) {
            newGifIndex = i;
            foundGif = true;
            break;
        }
    }
    
    // Queue frames from the new GIF
    if (foundGif && newGifIndex < g_gifs.size()) {
        // Clear existing queue
        g_frameQueue.clear();
        g_currentFrameIndex = 0;
        
        // Queue frames from the new GIF
        QueueFramesFromGif(newGifIndex);
        
        // Resize window to fit new GIF without forcing redraw
        ResizeWindowToGif(g_hwnd, g_gifs[newGifIndex].animation.image);
        
        // Set needsClear flag
        needsClear = true;
        
        // Start animation timer with consistent timing
        if (!g_frameQueue.empty()) {
            SetTimer(g_hwnd, ANIMATION_TIMER_ID, MIN_FRAME_DELAY, NULL);
        }
    } else {
        // If no valid GIF found, revert to previous state
        g_appState = prevState;
    }
    
    // Start a new timer for the next state change
    StartStateTimer();
}

// Modify LoadGifsFromFolder to queue frames after loading
bool LoadGifsFromFolder(const std::wstring& folderPath) {
    WIN32_FIND_DATAW findData;
    HANDLE hFind;
    std::wstring searchPath = folderPath + L"\\*.gif";
    
    hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring filename = findData.cFileName;
            std::wstring filePath = folderPath + L"\\" + filename;
            
            // Create a GIF info structure
            GifInfo gifInfo;
            gifInfo.filePath = filePath;
            gifInfo.type = GetGifTypeFromFilename(filename);
            gifInfo.animation.image = Gdiplus::Image::FromFile(filePath.c_str());
            gifInfo.animation.isPlaying = false;
            gifInfo.flipped = false;
            
            // Load frame info
            gifInfo.animation.frameDelays = LoadGifFrameInfo(gifInfo.animation.image);
            if (gifInfo.animation.frameDelays.empty()) {
                delete gifInfo.animation.image;
                continue;
            }
            
            // Get frame count
            UINT count = gifInfo.animation.image->GetFrameDimensionsCount();
            GUID* dimensionIDs = new GUID[count];
            gifInfo.animation.image->GetFrameDimensionsList(dimensionIDs, count);
            gifInfo.animation.frameCount = gifInfo.animation.image->GetFrameCount(&dimensionIDs[0]);
            delete[] dimensionIDs;
            
            // Add to our collection using move semantics
            g_gifs.push_back(std::move(gifInfo));
        }
    } while (FindNextFileW(hFind, &findData) != 0);
    
    FindClose(hFind);
    
    g_hasGifs = !g_gifs.empty();
    
    // After loading GIFs, resize window to fit the first GIF and queue its frames
    if (!g_gifs.empty() && g_gifs[0].animation.image != nullptr) {
        // Clear any existing queue
        g_frameQueue.clear();
        g_currentFrameIndex = 0;
        
        // Queue frames from the first GIF
        QueueFramesFromGif(0);
        
        // Resize window to fit the GIF
        ResizeWindowToGif(g_hwnd, g_gifs[0].animation.image);
        
        // Start animation timer
        if (!g_frameQueue.empty()) {
            SetTimer(g_hwnd, ANIMATION_TIMER_ID, g_frameQueue[0].delay, NULL);
        }
        
        // Force redraw
        InvalidateRect(g_hwnd, NULL, TRUE);
    }
    
    return g_hasGifs;
}

// Modify StartStateTimer to use consistent timing
void StartStateTimer() {
    // Kill any existing timers
    KillTimer(g_hwnd, TIMER_ID);
    KillTimer(g_hwnd, ANIMATION_TIMER_ID);
    
    if (g_appState == STATE_MOVE) {
        // For movement state, use consistent timing
        SetTimer(g_hwnd, TIMER_ID, MOVE_INTERVAL, NULL);
    } else {
        // For other states, use the random duration
        std::uniform_int_distribution<int> durationDist(MIN_STATE_DURATION, MAX_STATE_DURATION);
        int duration = durationDist(g_randomEngine);
        SetTimer(g_hwnd, TIMER_ID, duration, NULL);
    }
    
    // Start animation for current GIF with minimum frame delay
    if (g_hasGifs && g_currentGifIndex < g_gifs.size()) {
        UINT initialDelay = std::max(g_gifs[g_currentGifIndex].animation.frameDelays[0], MIN_FRAME_DELAY);
        SetTimer(g_hwnd, ANIMATION_TIMER_ID, initialDelay, NULL);
    }
}

// Modify MoveWindow to properly update flipped state
void MoveWindow() {
    if (g_appState != STATE_MOVE) {
        return;
    }
    
    RECT windowRect;
    GetWindowRect(g_hwnd, &windowRect);
    
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int windowWidth = windowRect.right - windowRect.left;
    
    // Calculate the new position
    int newX = windowRect.left;
    if (g_moveDirectionRight) {
        newX += MOVE_DISTANCE;
        
        // If we've reached the right edge, change direction
        if (newX + windowWidth > screenWidth) {
            g_moveDirectionRight = false;
            
            // Flip the GIF and update frame queue
            for (size_t i = 0; i < g_gifs.size(); i++) {
                if (g_gifs[i].type == MOVE) {
                    g_gifs[i].flipped = true;
                    // Re-queue frames with new flipped state
                    QueueFramesFromGif(i);
                }
            }
        }
    } else {
        newX -= MOVE_DISTANCE;
        
        // If we've reached the left edge, change direction
        if (newX < 0) {
            g_moveDirectionRight = true;
            
            // Flip the GIF and update frame queue
            for (size_t i = 0; i < g_gifs.size(); i++) {
                if (g_gifs[i].type == MOVE) {
                    g_gifs[i].flipped = false;
                    // Re-queue frames with new flipped state
                    QueueFramesFromGif(i);
                }
            }
        }
    }
    
    // Update the window position
    SetWindowPos(g_hwnd, NULL, newX, windowRect.top, 0, 0, 
                SWP_NOSIZE | SWP_NOZORDER);
    
    // Force redraw to ensure smooth animation
    InvalidateRect(g_hwnd, NULL, TRUE);
}

// Modify ToggleMenu to switch states when menu becomes visible
void ToggleMenu() {
    g_menuVisible = !g_menuVisible;
    ShowWindow(g_menuHwnd, g_menuVisible ? SW_SHOW : SW_HIDE);
    
    // Position menu window next to main window
    if (g_menuVisible) {
        RECT mainRect;
        GetWindowRect(g_hwnd, &mainRect);
        SetWindowPos(g_menuHwnd, NULL, 
                    mainRect.right, mainRect.top,
                    MENU_WIDTH, MENU_HEIGHT,
                    SWP_NOZORDER);
        
        // Switch states immediately when menu becomes visible
        if (!g_gifs.empty()) {
            SwitchToNextGif();
        }
    }
}

// Determine GIF type from filename
GifType GetGifTypeFromFilename(const std::wstring& filename) {
    std::wstring lowerFilename = filename;
    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);
    
    if (lowerFilename.find(L"move") != std::wstring::npos) {
        return MOVE;
    } else if (lowerFilename.find(L"wait") != std::wstring::npos) {
        return WAIT;
    } else if (lowerFilename.find(L"sit") != std::wstring::npos) {
        return SIT;
    } else if (lowerFilename.find(L"pick") != std::wstring::npos) {
        return PICK;
    } else {
        return MISC;
    }
}

// Modify CleanupGifs to ensure proper cleanup
void CleanupGifs() {
    // Kill any existing timers
    KillTimer(g_hwnd, TIMER_ID);
    KillTimer(g_hwnd, ANIMATION_TIMER_ID);
    
    // Clear frame queue
    g_frameQueue.clear();
    g_currentFrameIndex = 0;
    
    // Clean up layers
    if (g_topLayer) {
        delete g_topLayer;
        g_topLayer = nullptr;
    }
    if (g_bottomLayer) {
        delete g_bottomLayer;
        g_bottomLayer = nullptr;
    }
    
    // Clean up GIFs
    for (size_t i = 0; i < g_gifs.size(); i++) {
        if (g_gifs[i].animation.image != nullptr) {
            delete g_gifs[i].animation.image;
            g_gifs[i].animation.image = nullptr;
        }
    }
    
    g_gifs.clear();
    g_hasGifs = false;
} 