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
const int ANIMATION_INTERVAL = 50;  // 50ms for smooth animation
const int FRAME_BUFFER_SIZE = 3;  // Number of frames to buffer ahead
const int MIN_FRAME_DELAY = 16;   // Minimum frame delay (60 FPS)

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

// Main entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // Register the window class
    const wchar_t CLASS_NAME[] = L"ChibiViewerWindowClass";
    
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; // Remove background brush
    RegisterClassW(&wc);

    // Create the window
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

    // Set window transparency
    SetLayeredWindowAttributes(g_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    
    // Show the window
    ShowWindow(g_hwnd, nCmdShow);

    // Start with a default state timer
    StartStateTimer();

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
        case WM_CREATE:
            CreateButtons(hwnd);
            return 0;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Clear the window with black (transparent color)
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hdc, &clientRect, hBrush);
            DeleteObject(hBrush);
            
            // Draw the current frame from the queue
            if (!g_frameQueue.empty() && g_currentFrameIndex < g_frameQueue.size()) {
                Gdiplus::Graphics graphics(hdc);
                FrameInfo& frame = g_frameQueue[g_currentFrameIndex];
                
                // Select the correct frame
                GUID timeDimension = Gdiplus::FrameDimensionTime;
                frame.image->SelectActiveFrame(&timeDimension, frame.frameIndex);
                
                // Draw the frame
                graphics.DrawImage(frame.image, 0, 0);
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_ID && g_appMode == AUTOMATIC && !g_isPickMode) {
                UpdateAppState();
            } else if (wParam == ANIMATION_TIMER_ID && !g_frameQueue.empty()) {
                // Move to next frame in queue
                g_currentFrameIndex = (g_currentFrameIndex + 1) % g_frameQueue.size();
                
                // Set timer for next frame with minimum delay
                UINT nextDelay = std::max(g_frameQueue[g_currentFrameIndex].delay, MIN_FRAME_DELAY);
                SetTimer(hwnd, ANIMATION_TIMER_ID, nextDelay, NULL);
                
                // Force redraw
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;

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
            
        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED) {
                if ((HWND)lParam == g_quitButton) {
                    DestroyWindow(hwnd);
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
                                InvalidateRect(hwnd, NULL, TRUE);
                                
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

// Add helper function to resize window to fit GIF
void ResizeWindowToGif(HWND hwnd, Gdiplus::Image* gif) {
    if (!gif) return;
    
    // Get current window position
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    
    // Get GIF dimensions
    int gifWidth = gif->GetWidth();
    int gifHeight = gif->GetHeight();
    
    // Resize window to fit GIF
    SetWindowPos(hwnd, NULL, windowRect.left, windowRect.top, 
                gifWidth, gifHeight, SWP_NOZORDER);
}

// Modify QueueFramesFromGif to maintain a larger buffer
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
            
            // Ensure minimum frame delay for smooth animation
            frame.delay = std::max(gif.animation.frameDelays[i], MIN_FRAME_DELAY);
            frame.flipped = gif.flipped;
            g_frameQueue.push_back(frame);
        }
    }
}

// Modify SwitchToNextGif to queue frames instead of switching GIFs
void SwitchToNextGif() {
    if (g_gifs.empty()) {
        return;
    }
    
    // Store current state
    AppState prevState = g_appState;
    
    // In manual mode, cycle through states: MOVE -> WAIT -> SIT -> MISC
    if (g_appState == STATE_MOVE) {
        g_appState = STATE_WAIT;
    } else if (g_appState == STATE_WAIT) {
        g_appState = STATE_SIT;
    } else if (g_appState == STATE_SIT) {
        g_appState = STATE_MISC;
        g_miscGifIndex = 0;
    } else {
        // If we're at MISC, try to find the next MISC gif
        int miscCount = 0;
        for (size_t i = 0; i < g_gifs.size(); i++) {
            if (g_gifs[i].type == MISC) {
                miscCount++;
            }
        }
        
        // If we have more MISC gifs, increment the index
        if (miscCount > 1) {
            g_miscGifIndex = (g_miscGifIndex + 1) % miscCount;
        } else {
            // Otherwise go back to MOVE
            g_appState = STATE_MOVE;
        }
    }
    
    // Find the appropriate GIF for the new state
    size_t newGifIndex = g_currentGifIndex;
    bool foundGif = false;
    
    if (g_appState == STATE_PICK) {
        for (size_t i = 0; i < g_gifs.size(); i++) {
            if (g_gifs[i].type == PICK) {
                newGifIndex = i;
                foundGif = true;
                break;
            }
        }
    } else {
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
    }
    
    // Queue frames from the new GIF
    if (foundGif && newGifIndex < g_gifs.size()) {
        // Clear existing queue
        g_frameQueue.clear();
        g_currentFrameIndex = 0;
        
        // Queue frames from the new GIF
        QueueFramesFromGif(newGifIndex);
        
        // Resize window to fit new GIF
        ResizeWindowToGif(g_hwnd, g_gifs[newGifIndex].animation.image);
        
        // Start animation timer
        if (!g_frameQueue.empty()) {
            SetTimer(g_hwnd, ANIMATION_TIMER_ID, g_frameQueue[0].delay, NULL);
        }
    } else {
        // If no valid GIF found, revert to previous state
        g_appState = prevState;
    }
}

// Modify UpdateAppState to queue frames instead of switching GIFs
void UpdateAppState() {
    if (g_gifs.empty()) {
        return;
    }
    
    // Store current state
    AppState prevState = g_appState;
    
    // Generate a random number between 0 and 3 for the next state
    std::uniform_int_distribution<int> stateDist(0, 3);
    int nextState = stateDist(g_randomEngine);
    
    // Move the dirDist declaration outside the switch
    std::uniform_int_distribution<int> dirDist(0, 1);
    
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
            
            // Start moving the window
            MoveWindow();
            break;
            
        case 1:
            g_appState = STATE_WAIT;
            break;
            
        case 2:
            g_appState = STATE_SIT;
            break;
            
        case 3:
            g_appState = STATE_MISC;
            
            // Randomly select a MISC gif if we have multiple
            int miscCount = 0;
            for (size_t i = 0; i < g_gifs.size(); i++) {
                if (g_gifs[i].type == MISC) {
                    miscCount++;
                }
            }
            
            if (miscCount > 0) {
                std::uniform_int_distribution<int> miscDist(0, miscCount - 1);
                g_miscGifIndex = miscDist(g_randomEngine);
            }
            break;
    }
    
    // Find the appropriate GIF for the new state
    size_t newGifIndex = g_currentGifIndex;
    bool foundGif = false;
    
    if (g_appState == STATE_PICK) {
        for (size_t i = 0; i < g_gifs.size(); i++) {
            if (g_gifs[i].type == PICK) {
                newGifIndex = i;
                foundGif = true;
                break;
            }
        }
    } else {
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
    }
    
    // Queue frames from the new GIF
    if (foundGif && newGifIndex < g_gifs.size()) {
        // Clear existing queue
        g_frameQueue.clear();
        g_currentFrameIndex = 0;
        
        // Queue frames from the new GIF
        QueueFramesFromGif(newGifIndex);
        
        // Resize window to fit new GIF
        ResizeWindowToGif(g_hwnd, g_gifs[newGifIndex].animation.image);
        
        // Start animation timer
        if (!g_frameQueue.empty()) {
            SetTimer(g_hwnd, ANIMATION_TIMER_ID, g_frameQueue[0].delay, NULL);
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

// Modify StartStateTimer to use minimum frame delay
void StartStateTimer() {
    // Kill any existing timers
    KillTimer(g_hwnd, TIMER_ID);
    KillTimer(g_hwnd, ANIMATION_TIMER_ID);
    
    if (g_appState == STATE_MOVE) {
        // For movement state, use a shorter timer
        SetTimer(g_hwnd, TIMER_ID, 50, NULL);
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

// Move the window in the appropriate direction
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
        newX += 10;  // Increased movement speed
        
        // If we've reached the right edge, change direction
        if (newX + windowWidth > screenWidth) {
            g_moveDirectionRight = false;
            
            // Flip the GIF
            for (size_t i = 0; i < g_gifs.size(); i++) {
                if (g_gifs[i].type == MOVE) {
                    g_gifs[i].flipped = true;
                }
            }
        }
    } else {
        newX -= 10;  // Increased movement speed
        
        // If we've reached the left edge, change direction
        if (newX < 0) {
            g_moveDirectionRight = true;
            
            // Flip the GIF
            for (size_t i = 0; i < g_gifs.size(); i++) {
                if (g_gifs[i].type == MOVE) {
                    g_gifs[i].flipped = false;
                }
            }
        }
    }
    
    // Update the window position
    SetWindowPos(g_hwnd, NULL, newX, windowRect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    
    // Schedule the next movement with a shorter delay
    if (g_appState == STATE_MOVE && !g_isPickMode) {
        SetTimer(g_hwnd, TIMER_ID, 50, NULL);  // 50ms timer for smoother movement
    }
}

// Toggle the menu visibility
void ToggleMenu() {
    g_menuVisible = !g_menuVisible;
    
    if (g_importButton != NULL && g_quitButton != NULL) {
        ShowWindow(g_importButton, g_menuVisible ? SW_SHOW : SW_HIDE);
        ShowWindow(g_quitButton, g_menuVisible ? SW_SHOW : SW_HIDE);
    }
}

// Create the menu buttons
void CreateButtons(HWND hwnd) {
    // Create import button
    g_importButton = CreateWindowW(
        L"BUTTON", L"Select GIF Folder",
        WS_CHILD | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
        (MENU_WIDTH - BUTTON_WIDTH) / 2, 
        BUTTON_MARGIN,
        BUTTON_WIDTH, BUTTON_HEIGHT,
        hwnd, NULL, GetModuleHandle(NULL), NULL
    );
    
    // Create quit button
    g_quitButton = CreateWindowW(
        L"BUTTON", L"Exit Program",
        WS_CHILD | BS_PUSHBUTTON | BS_CENTER | BS_VCENTER,
        (MENU_WIDTH - BUTTON_WIDTH) / 2, 
        BUTTON_MARGIN * 2 + BUTTON_HEIGHT,
        BUTTON_WIDTH, BUTTON_HEIGHT,
        hwnd, NULL, GetModuleHandle(NULL), NULL
    );
    
    // Set font for all controls
    HFONT hFont = CreateFontW(
        18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    
    SendMessage(g_importButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(g_quitButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Show buttons by default
    ShowWindow(g_importButton, SW_SHOW);
    ShowWindow(g_quitButton, SW_SHOW);
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

// Modify CleanupGifs to clean up frame queue
void CleanupGifs() {
    // Kill any existing timers
    KillTimer(g_hwnd, TIMER_ID);
    KillTimer(g_hwnd, ANIMATION_TIMER_ID);
    
    // Clear frame queue
    g_frameQueue.clear();
    g_currentFrameIndex = 0;
    
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