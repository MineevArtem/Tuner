#define WIN32_LEAN_AND_MEAN
#include <tchar.h>
#include <windows.h>
#include <math.h>
#include <process.h>
#include <cstdlib>

// константы
#define MAXBUF 1024 // размер кольцевого буфера
#define BUFCOUNT 32 // по сколько байт считываем из буфера
#define WM_WAVEDATA (WM_USER+0) // сообщение о завершении записи в буфер

// глобальные переменнные
CRITICAL_SECTION cs;
HANDLE hStopDataEvent;
HWND hwnd, hDataWnd;
int Buffer[MAXBUF]; //кольцевой буфер
int first=0, last=0;
bool fStop = 0;

// заголовки оконных функций
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK DataWndProc(HWND, UINT, WPARAM, LPARAM);
VOID DataThread(PVOID pvoid);

// заголовки вспомогательных функций
int PerformDrawing(int* buf, int count);
int ReadBuffer(int* buf, int count);
int WriteBuffer(int* buf, int count);

// главная функция
int WINAPI _tWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     PTSTR     pCmdLine,
    _In_     int       nShowCmd)
{
    static TCHAR szAppName[] = TEXT("Tuner");
    MSG          msg;
    WNDCLASS     wndclass;

    wndclass.style = CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc = WndProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = hInstance;
    wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wndclass.lpszMenuName = NULL;
    wndclass.lpszClassName = szAppName;
    RegisterClass(&wndclass);

    InitializeCriticalSection(&cs);

    hwnd = CreateWindow(szAppName, TEXT("Tuner"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1000, 512,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteCriticalSection(&cs);
    return (int)msg.wParam;  // WM_QUIT
}

// окно отрисовки/ интерфейсное окно
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static TCHAR szClassName[] = TEXT("DataReceiver");
    WNDCLASS     wndclass;

    switch (message)
    {
    case WM_CREATE:
        wndclass.style = CS_HREDRAW | CS_VREDRAW;
        wndclass.lpfnWndProc = DataWndProc;
        wndclass.cbClsExtra = 0;
        wndclass.cbWndExtra = 0;
        wndclass.hInstance = ((LPCREATESTRUCT)lParam)->hInstance;
        wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
        wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wndclass.lpszMenuName = NULL;
        wndclass.lpszClassName = szClassName;
        RegisterClass(&wndclass);

        hDataWnd = CreateWindow(szClassName, TEXT(""),
            WS_OVERLAPPEDWINDOW,
            0, 0, 0, 0,
            NULL, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        return 0;

    case WM_DESTROY:
        DestroyWindow(hDataWnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}


LRESULT CALLBACK DataWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int buf[BUFCOUNT], sze;
    switch (message)
    {
    case WM_CREATE:
        hStopDataEvent = CreateEvent(NULL, true /*manual*/, false, NULL);
        _beginthread(DataThread, 0, NULL);
        return 0;
    case WM_WAVEDATA:
        sze = ReadBuffer(buf, BUFCOUNT);
        PerformDrawing(buf, sze);
        return 0;
    case WM_DESTROY:
        //fStop = true;
        WaitForSingleObject(DataThread, 10000);
        return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

// поток записи
VOID DataThread(PVOID pvoid) {
    int buf[BUFCOUNT];
    while (!fStop) {
        for (int i = 0; i < BUFCOUNT; i++) {
            buf[i] = rand() % 256;
        }
        WriteBuffer(buf, BUFCOUNT);
        Sleep(100);
    }
    _endthread();
}

// рисование
int PerformDrawing(int* buf, int count) {
    static int curX = 0, curY=0;
    const int maxPosition = 1000;
    HDC hdc;

    hdc = GetDC(hwnd);
    MoveToEx(hdc, curX, curY, NULL);
    for (int i = 0; i < count; i++) {
        if (curX == 0) {
            InvalidateRect(hwnd, NULL, true);
            UpdateWindow(hwnd);  // Очистить окно
            MoveToEx(hdc, curX, 384 - buf[i], NULL);
        }
        LineTo(hdc, curX, 384 - buf[i]);
        if (curX >= maxPosition)  curX = 0; // Начать развертку с начала для следующего байта
        else curX++;
    }
    curY = 384 - buf[count-1];

    ReleaseDC(hwnd, hdc);
    return 0;
}

// чтение
int ReadBuffer(int* buf, int count) {
    //Возвращает количество реально прочитанных байтов
    int sze = 0;
    EnterCriticalSection(&cs);
    if (first == last) sze = 0;
    else if (last > first) {
        for (sze = 0; sze < count; sze++) {
            buf[sze] = Buffer[first];
            first++;
            if (first == last) break;
        }
    }
    else { // if last < first
        for (sze = 0; sze < count; sze++) {
            buf[sze] = Buffer[first];
            first++;
            if (first == MAXBUF) first = 0;
            if (first == last) break;
        }
    }
    LeaveCriticalSection(&cs);
    return sze;
}

// запись в буфер
int WriteBuffer(int* buf, int count) {
    EnterCriticalSection(&cs);
    for (int i = 0; i < count; i++) {
        Buffer[last] = buf[i];
        last++;
        if (last == MAXBUF) last = 0;
    }
    PostMessage(hDataWnd, WM_WAVEDATA, 0, 0);
    LeaveCriticalSection(&cs);
    return 0;
}