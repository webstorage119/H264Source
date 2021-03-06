/** @file

MODULE				: H264

FILE NAME			: DLLSetup.cpp

DESCRIPTION			: H264 source filter registration

LICENSE: Software License Agreement (BSD License)

Copyright (c) 2012, CSIR
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
* Neither the name of the CSIR nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

===========================================================================
*/
#include "stdafx.h"
#include "H264Source.h"
#include "H264Properties.h"

// Note: It is better to register no media types than to register a partial 
// media type (subtype == GUID_NULL) because that can slow down intelligent connect 
// for everyone else.

// For a specialized source filter like this, it is best to leave out the 
// AMOVIESETUP_FILTER altogether, so that the filter is not available for 
// intelligent connect. Instead, use the CLSID to create the filter or just 
// use 'new' in your application.


// Filter setup data
const AMOVIESETUP_MEDIATYPE sudOpPinTypes =
{
  &MEDIATYPE_Video,       // Major type
  &MEDIASUBTYPE_H264      // Minor type
  //&MEDIASUBTYPE_H264M
};


const AMOVIESETUP_PIN sudOutputPinBitmap = 
{
  L"Output",      // Obsolete, not used.
  FALSE,          // Is this pin rendered?
  TRUE,           // Is it an output pin?
  FALSE,          // Can the filter create zero instances?
  FALSE,          // Does the filter create multiple instances?
  &CLSID_NULL,    // Obsolete.
  NULL,           // Obsolete.
  1,              // Number of media types.
  &sudOpPinTypes  // Pointer to media types.
};

const AMOVIESETUP_FILTER sudPushSourceBitmap =
{
  &CLSID_VPP_H264Source,  // Filter CLSID
  g_wszH264Source,        // String name
  MERIT_NORMAL,           // Filter merit
  1,                      // Number pins
  &sudOutputPinBitmap     // Pin details
};


// List of class IDs and creator functions for the class factory. This
// provides the link between the OLE entry point in the DLL and an object
// being created. The class factory will call the static CreateInstance.
// We provide a set of filters in this one DLL.

CFactoryTemplate g_Templates[] = 
{
  { 
    g_wszH264Source,                    // Name
      &CLSID_VPP_H264Source,            // CLSID
      H264SourceFilter::CreateInstance, // Method to create an instance of MyComponent
      NULL,                             // Initialization function
      &sudPushSourceBitmap              // Set-up information (for filters)
  },
  // This entry is for the property page.
  { 
    L"H264 Source Filter Properties",
      &CLSID_H264Properties,
      H264Properties::CreateInstance, 
      NULL, 
      NULL
    }
};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);    



////////////////////////////////////////////////////////////////////////
//
// Exported entry points for registration and unregistration 
// (in this case they only call through to default implementations).
//
////////////////////////////////////////////////////////////////////////

STDAPI DllRegisterServer()
{
  return AMovieDllRegisterServer2( TRUE );
}

STDAPI DllUnregisterServer()
{
  return AMovieDllRegisterServer2( FALSE );
}

//
// DllEntryPoint
//
extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL APIENTRY DllMain(HANDLE hModule, 
  DWORD  dwReason, 
  LPVOID lpReserved)
{
  return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}

