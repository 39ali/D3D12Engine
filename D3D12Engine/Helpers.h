#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cassert>
inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
		    assert(0);
}