#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <wrl.h>
using namespace Microsoft::WRL;  //  Needed for ComPtr<> .
// d3d12 headers
#include <DirectXMath.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include "Helpers.h"
#include "Vendors\D3Dx12\d3dx12.h"
//
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>

const uint8_t g_NumFrames = 3;

uint32_t g_ClientWidth = 1280;
uint32_t g_ClientHeight = 720;

bool g_IsInitializd = false;

HWND g_Hnwd;  // window Handle
RECT g_WindowRect;

ComPtr<ID3D12Device2> g_Device;
ComPtr<IDXGISwapChain4> g_SwapChain;
ComPtr<ID3D12Resource> g_BackBuffers[g_NumFrames];
ComPtr<ID3D12CommandList> g_CommandList;
ComPtr<ID3D12CommandAllocator> g_CommandAllocators[g_NumFrames];
ComPtr<ID3D12DescriptorHeap> g_RtvDescriptorHeap;
ComPtr<ID3D12CommandQueue> g_CommandQueue;

UINT g_RtvDescriptorSize;
UINT g_CurrentBackBufferIndex;

ComPtr<ID3D12Fence> g_Fence;
uint64_t g_FenceValue = 0;
uint64_t g_FrameFenceValues[g_NumFrames] = {};
HANDLE g_FenceEvent;

bool g_Vsync = true;
bool g_TearingSupported = false;
bool g_Fullscreen = false;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM) { return{}; };

void EnableDEbugLayer() {
#ifndef NDEBUG
  ComPtr<ID3D12Debug> debuginterface;
  ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debuginterface)));
  debuginterface->EnableDebugLayer();
#endif
}

void RegisterWindowClass(HINSTANCE hInst, const wchar_t* windowClassName) {
  WNDCLASSEXW windowClass = {};
  windowClass.cbSize = sizeof(WNDCLASSEX);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = &WndProc;
  windowClass.cbClsExtra = 0;
  windowClass.cbWndExtra = 0;
  windowClass.hInstance = hInst;
  windowClass.hIcon = ::LoadIcon(hInst, NULL);
  windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
  windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  windowClass.lpszMenuName = NULL;
  windowClass.lpszClassName = windowClassName;
  windowClass.hIconSm = ::LoadIcon(hInst, NULL);
  static ATOM atom = ::RegisterClassExW(&windowClass);
  assert(atom > 0);
}

HWND CreateWin(const wchar_t* windowClassName, HINSTANCE hInst,
               const wchar_t* windowTitle, uint32_t width, uint32_t height) {
  int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
  int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

  RECT windowRect = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
  ::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

  int windowWidth = windowRect.right - windowRect.left;
  int windowHeight = windowRect.bottom - windowRect.top;

  int windowX = std::max<int>(0, (screenWidth - windowWidth / 2));
  int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

  HWND hwnd = ::CreateWindowExW(
      NULL, windowClassName, windowTitle, WS_OVERLAPPEDWINDOW, windowX, windowY,
      windowWidth, windowHeight, NULL, NULL, hInst, nullptr);
  assert(hwnd && "Failed!: window  was not created ");
  return hwnd;
}

ComPtr<IDXGIAdapter4> GetAdapter() 
{
	ComPtr<IDXGIFactory4> dxgiFactory;
	UINT createFactoryFlags = 0;
#ifndef NDEBUG
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	ComPtr<IDXGIAdapter1> dxgiAdapter1;
	ComPtr<IDXGIAdapter4> dxgiAdapter4;
	SIZE_T maxDedicatedVideoMemory = 0;
	for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i) 
	{
		DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
		dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

		if ((dxgiAdapterDesc1.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
			SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
			dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory) {
			
			maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
			ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
		}
	}
	return dxgiAdapter4;
}

ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter){
	ComPtr<ID3D12Device2> d3d12Device2;
	ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));

#ifndef NDEBUG
	ComPtr<ID3D12InfoQueue> pInfoQueue;
	if(SUCCEEDED(d3d12Device2.As(&pInfoQueue)))
	{
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

		D3D12_MESSAGE_SEVERITY sev[] = {D3D12_MESSAGE_SEVERITY_INFO};

		//msgs to ingore
		D3D12_MESSAGE_ID ids[] = {
			D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
			D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
			D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
		};

		D3D12_INFO_QUEUE_FILTER newFilter = {};
		newFilter.DenyList.NumSeverities = _countof(sev);
		newFilter.DenyList.NumIDs = _countof(ids);
		newFilter.DenyList.pSeverityList = sev;
		newFilter.DenyList.pIDList = ids;
		ThrowIfFailed(pInfoQueue->PushStorageFilter(&newFilter));

	}
#endif
	return d3d12Device2;
}


//ComPtr<ID3D12CommandQueue
// 
void main() {
  int n;
  std::cin >> n;
}
