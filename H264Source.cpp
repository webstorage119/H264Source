/** @file

MODULE				: H264Source

FILE NAME			: H264Source.cpp

DESCRIPTION			: H264 source filter implementation

LICENSE: Software License Agreement (BSD License)

Copyright (c) 2014, CSIR
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
#include <sstream>
#include "H264Source.h"
#include "H264OutputPin.h"
#include <H264v2/H264v2.h>
#include <CodecUtils/ICodecv2.h>
#include "Conversion.h"
#include "StringUtil.h"

const AMOVIESETUP_MEDIATYPE sudOpPinTypes =
{
  &MEDIATYPE_Video,       // Major type
  &MEDIASUBTYPE_NULL      // Minor type
};

// TODO: parse width and height from sequence parameter set
#define DEFAULT_WIDTH 176
#define DEFAULT_HEIGHT 144

const unsigned char g_startCode_3[] = { 0, 0, 1};

using namespace artist;

CUnknown * WINAPI H264SourceFilter::CreateInstance(IUnknown *pUnk, HRESULT *phr)
{
  H264SourceFilter *pNewFilter = new H264SourceFilter(pUnk, phr );
  if (phr)
  {
    if (pNewFilter == NULL) 
      *phr = E_OUTOFMEMORY;
    else
      *phr = S_OK;
  }
  return pNewFilter;
}

const unsigned MINIMUM_BUFFER_SIZE = 1024;
H264SourceFilter::H264SourceFilter(IUnknown *pUnk, HRESULT *phr)
  : CSource(NAME("CSIR VPP H264 Source"), pUnk, CLSID_VPP_H264Source),
  m_iWidth(0),
  m_iHeight(0),
  m_iFramesPerSecond(25),
  m_uiCurrentBufferSize(MINIMUM_BUFFER_SIZE),
  m_pBuffer(NULL),
  m_uiBytesInBuffer(0),
  m_uiCurrentNalUnitSize(0),
  m_uiCurrentNalUnitStartPos(0),
  m_pSeqParamSet(0),
  m_uiSeqParamSetLen(0),
  m_pPicParamSet(0),
  m_uiPicParamSetLen(0),
  m_uiCurrentStartCodeSize(0),
  m_iFileSize(0),
  m_bAnalyseOnLoad(true)
{
  // Init CSettingsInterface
  initParameters();

  m_pPin = new H264OutputPin(phr, this);

  if (phr)
  {
    if (m_pPin == NULL)
      *phr = E_OUTOFMEMORY;
    else
      *phr = S_OK;
  }

  H264v2Factory factory;
  m_pCodec = factory.GetCodecInstance();
  // Set default codec properties 
  if (!m_pCodec)
  {
    SetLastError("Unable to create H264 Encoder from Factory.", true);
    *phr = E_OUTOFMEMORY;
  }
}

H264SourceFilter::~H264SourceFilter()
{
  if (m_pSeqParamSet) delete[] m_pSeqParamSet;
  if (m_pPicParamSet) delete[] m_pPicParamSet;

  if ( m_in1.is_open() )
  {
    m_in1.close();
  }
  if (m_pBuffer)
    delete[] m_pBuffer;

  delete m_pPin;

  if (m_pCodec)
  {
    m_pCodec->Close();
    H264v2Factory factory;
    factory.ReleaseCodecInstance(m_pCodec);
  }
}

void H264SourceFilter::reset()
{
  m_pPin->m_iCurrentFrame = 0;
  m_uiBytesInBuffer = 0;
  m_uiCurrentNalUnitSize = 0;
  m_uiCurrentNalUnitStartPos = 0;
  m_in1.clear(); // clear for next play
  m_in1.seekg( 0 , std::ios::beg );
}

STDMETHODIMP H264SourceFilter::Load( LPCOLESTR lpwszFileName, const AM_MEDIA_TYPE *pmt )
{
  // Store the URL
  m_sFile = StringUtil::wideStringToString(lpwszFileName);

  m_in1.open(m_sFile.c_str(), std::ifstream::in | std::ifstream::binary);
  if ( m_in1.is_open() )
  {
    m_in1.seekg( 0, std::ios::end );
    m_iFileSize = m_in1.tellg();
    m_in1.seekg( 0 , std::ios::beg );

    recalculate();
    m_pBuffer = new unsigned char[m_uiCurrentBufferSize];

    bool bSps = false, bPps = false;
    // now try to search for SPS and PPS
    while (true)
    {
      if (!readNalUnit())
      {
        //// failed to find SPS and PPS
        //return E_FAIL;
        break;
      }

      // check for SPS and PPS
      if (isSps(m_pBuffer[4]))
      {
        // only store first one
        if (bSps) continue;
        bSps = true;
        if (m_pSeqParamSet) delete[] m_pSeqParamSet;
        m_pSeqParamSet = new unsigned char[m_uiCurrentNalUnitSize];
        memcpy(m_pSeqParamSet, m_pBuffer, m_uiCurrentNalUnitSize);
        m_uiSeqParamSetLen = static_cast<uint32_t>(m_uiCurrentNalUnitSize);

        m_vParameterSets.push_back(m_uiCurrentNalUnitStartPos);
      }
      else if (isPps(m_pBuffer[4]))
      {
        // only store first one
        if (bPps) continue;
        bPps = true;
        if (m_pPicParamSet) delete[] m_pPicParamSet;
        m_pPicParamSet = new unsigned char[m_uiCurrentNalUnitSize];
        memcpy(m_pPicParamSet, m_pBuffer, m_uiCurrentNalUnitSize);
        m_uiPicParamSetLen = static_cast<uint32_t>(m_uiCurrentNalUnitSize);

        m_vParameterSets.push_back(m_uiCurrentNalUnitStartPos);
      }
      else
      {
        if (isIdrFrame(m_pBuffer[4]))
        {
          m_vIdrFrames.push_back(m_uiCurrentNalUnitStartPos);
          m_vFrames.push_back(m_uiCurrentNalUnitStartPos);
        }
        else
        {
          m_vFrames.push_back(m_uiCurrentNalUnitStartPos);
        }
      }

      // Recompile to avoid analysis on load
      if (!m_bAnalyseOnLoad)
        if (bSps && bPps) break;
    }

    // check if we found the parameter sets
    if (!(bSps && bPps)) return E_FAIL;
    // parse sequence parameter set
    // Only fail if using the RTVC decoder
    // The MS one might be more forgiving
    if (!parseParameterSets() && m_bUseRtvcH264)
    {
      // don't fail if m_bUseRtvcH264 == false
      SetLastError(("Failed to parse parameter required for RTVC codec sets in: " + m_sFile).c_str(), true);
      return E_FAIL;
    }

    // reset file pointer
    reset();
    return S_OK;
  }
  else
  {
    SetLastError(("Failed to open file: " + m_sFile).c_str(), true);
    return E_FAIL;
  }
}

bool H264SourceFilter::parseParameterSets()
{
  BYTE buffer[256];
  // now configure the codec
  int nResult = m_pCodec->Decode(m_pSeqParamSet, m_uiSeqParamSetLen * 8, buffer);
  if (nResult)
  {
    //Encoding was successful
    nResult = m_pCodec->Decode(m_pPicParamSet, m_uiPicParamSetLen * 8, buffer);
    if (nResult)
    {
      m_pCodec->SetParameter((char *)("generate param set on open"), "0");
      if (!m_pCodec->Open())
      {
        //Houston: we have a failure
        char* szErrorStr = m_pCodec->GetErrorStr();
        printf("%s\n", szErrorStr);
        // report the error to the main application
        SetLastError(szErrorStr, true);
        return false;
      }
      else
      {
        // success
        int len = 0;
        int res = m_pCodec->GetParameter("width", &len, buffer);
        ASSERT(res);
        buffer[len] = 0;
        m_iWidth = atoi((const char*)buffer);
        res = m_pCodec->GetParameter("height", &len, buffer);
        ASSERT(res);
        buffer[len] = 0;
        m_iHeight = atoi((const char*)buffer);
        std::ostringstream ostr;
        ostr << m_iWidth << "x" << m_iHeight;
        m_sDimensions = ostr.str();
        return true;
      }
    }
    else
    {
      //An error has occurred
      DbgLog((LOG_TRACE, 0, TEXT("H264 Decode Error: %s"), m_pCodec->GetErrorStr()));
      std::string sError = "H264 decode error has occurred: " + std::string(m_pCodec->GetErrorStr());
      SetLastError(sError.c_str(), true);
      return false;
    }
  }
  else
  {
    //An error has occurred
    DbgLog((LOG_TRACE, 0, TEXT("H264 Decode Error: %s"), m_pCodec->GetErrorStr()));
    std::string sError = "H264 decode error has occurred: " + std::string(m_pCodec->GetErrorStr());
    SetLastError(sError.c_str(), true);
    return false;
  }
}

STDMETHODIMP H264SourceFilter::GetCurFile( LPOLESTR * ppszFileName, AM_MEDIA_TYPE *pmt )
{
  *ppszFileName = NULL;

  if (m_sFile.length() != 0) 
  {
    std::wstring wsFileName = StringUtil::stringToWideString(m_sFile);
    DWORD n = sizeof(WCHAR)*(1 + lstrlenW(wsFileName.c_str()));
    *ppszFileName = (LPOLESTR)CoTaskMemAlloc(n);
    if (*ppszFileName != NULL) {
      CopyMemory(*ppszFileName, wsFileName.c_str(), n);
    }
    return S_OK;
  }
  return NOERROR;
}

STDMETHODIMP H264SourceFilter::NonDelegatingQueryInterface( REFIID riid, void **ppv )
{
  if(riid == (IID_ISettingsInterface))
  {
    return GetInterface((ISettingsInterface*) this, ppv);
  }
  else if (riid == IID_IStatusInterface)
  {
    return GetInterface((IStatusInterface*) this, ppv);
  }
  else if (riid == IID_IFileSourceFilter)
  {
    return GetInterface((IFileSourceFilter*) this, ppv);
  }
  else if (riid == IID_ISpecifyPropertyPages)
  {
    return GetInterface(static_cast<ISpecifyPropertyPages*>(this), ppv);
  }
  else if (riid == IID_IMediaSeeking)
  {
    return GetInterface((IMediaSeeking*) this, ppv);
  }
  else
  {
    return CSource::NonDelegatingQueryInterface(riid, ppv);
  }
}

STDMETHODIMP H264SourceFilter::Stop()
{
  reset();
  return CSource::Stop();
}

STDMETHODIMP H264SourceFilter::SetParameter( const char* type, const char* value )
{
#if 0
  if (strcmp(type, SOURCE_DIMENSIONS) == 0)
  {
    if ( setDimensions(value) )
      return CSettingsInterface::SetParameter(type, value);
    else
      return E_FAIL;
  }
  else
  {
    HRESULT hr = CSettingsInterface::SetParameter(type, value);
    if (SUCCEEDED(hr)) recalculate();
    return hr;
  }
#else
  return CSettingsInterface::SetParameter(type, value);
#endif
}

void H264SourceFilter::recalculate()
{
  m_pPin->m_rtFrameLength = UNITS/m_iFramesPerSecond;
}

int H264SourceFilter::findIndexOfNextStartCode(unsigned uiStartingPos, unsigned& uiLengthOfStartCode)
{
  for (size_t i = uiStartingPos; i < m_uiBytesInBuffer - 4; ++i)
  {
    if (memcmp((char*)&m_pBuffer[i], g_startCode_3, 3) == 0)
    {
      if (m_pBuffer[i - 1] == 0)
        uiLengthOfStartCode = 4;
      else
        uiLengthOfStartCode = 3;
      return i;
    }
  }
  return -1;
}

bool H264SourceFilter::readNalUnit()
{
  if (m_in1.is_open())
  {
    // shift previous NAL unit out of buffer
    if (m_uiCurrentNalUnitSize > 0 && m_uiBytesInBuffer >= m_uiCurrentNalUnitSize)
    {
      ASSERT(m_uiBytesInBuffer >= m_uiCurrentNalUnitSize);
      if (m_uiBytesInBuffer > m_uiCurrentNalUnitSize)
      {
        memmove(m_pBuffer, m_pBuffer + m_uiCurrentNalUnitSize, m_uiBytesInBuffer - m_uiCurrentNalUnitSize);
        m_uiCurrentStartCodeSize = (memcmp(g_startCode_3, m_pBuffer, 3) == 0) ? 3 : 4;
      }

      m_uiBytesInBuffer -= m_uiCurrentNalUnitSize;
      m_uiCurrentNalUnitStartPos += m_uiCurrentNalUnitSize;
      m_uiCurrentNalUnitSize = 0;
    }

    // try and keep buffer full
    m_in1.read( (char*)m_pBuffer + m_uiBytesInBuffer, m_uiCurrentBufferSize - m_uiBytesInBuffer );
    m_uiBytesInBuffer += m_in1.gcount();

    if (m_uiBytesInBuffer < 4 ) return false;

    // check if we have enough data in the buffer to read the NAL unit
    unsigned uiStartCodeLen = 0;
    if (memcmp(g_startCode_3, m_pBuffer, 3) == 0)
      uiStartCodeLen = 3;

    if (memcmp(g_startCode_3, m_pBuffer + 1, 3) == 0)
      uiStartCodeLen = 4;

    if (uiStartCodeLen > 0)
    {
      uint64_t uiStartingPos = uiStartCodeLen;
      while (true)
      {
        // need to find next start code or EOF
        int iIndex = findIndexOfNextStartCode(uiStartingPos, uiStartCodeLen);
        if (iIndex != -1)
        {
          // found the end of the nal unit
          m_uiCurrentNalUnitSize = iIndex;
          // compensate for 4 byte start codes
          if (uiStartCodeLen == 4)
          {
            --m_uiCurrentNalUnitSize;
          }
          return true;
        }
        else
        {
          // didn't find next start code: read more data if not EOF
          if (m_in1.eof())
          {
            if (m_uiBytesInBuffer > 0)
            {
              m_uiCurrentNalUnitSize = m_uiBytesInBuffer;
              return true; // EOF: return last NAL unit
            }
            else
            {
              return false;
            }
          }
          if (m_in1.fail()) return false;

          // expand buffer
          BYTE* pNewBuffer = new BYTE[m_uiCurrentBufferSize * 2];
          memcpy(pNewBuffer, m_pBuffer, m_uiCurrentBufferSize);
          delete[] m_pBuffer;
          m_pBuffer = pNewBuffer;
          uiStartingPos = m_uiCurrentBufferSize;
          
          // read new data
          m_in1.read( (char*)m_pBuffer + m_uiBytesInBuffer, m_uiCurrentBufferSize);
          m_uiBytesInBuffer += m_in1.gcount();

          m_uiCurrentBufferSize *= 2;
        }
      }
    }
    else
    {
      // no start code: exit
      return false;
    }
  }
  return false;
}

void H264SourceFilter::skipToFrame(unsigned uiFrameNumber)
{
  m_uiCurrentNalUnitSize = 0;
  m_uiBytesInBuffer = 0;
  m_pPin->m_iCurrentFrame = uiFrameNumber;
  m_uiCurrentNalUnitStartPos = m_vFrames[uiFrameNumber];
  m_in1.clear(); // clear for next play
  m_in1.seekg( m_uiCurrentNalUnitStartPos , std::ios::beg );
}

// IMediaSeeking
HRESULT H264SourceFilter::CheckCapabilities(DWORD *pCapabilities)
{
  DWORD ourCap = AM_SEEKING_CanSeekAbsolute | AM_SEEKING_CanGetDuration;
  DWORD cap = *pCapabilities;
  DWORD match = cap & ourCap;
  if (match == ourCap) return S_OK;
  else if (match > 0)
  {
    *pCapabilities = match;
    return S_FALSE;
  }
  else
  {
    *pCapabilities = 0;
    return E_FAIL;
  }
}

HRESULT H264SourceFilter::ConvertTimeFormat( LONGLONG *pTarget, const GUID *pTargetFormat, LONGLONG Source, const GUID *pSourceFormat)
{
  return E_NOTIMPL;
}

HRESULT H264SourceFilter::GetAvailable(LONGLONG *pEarliest,  LONGLONG *pLatest)
{
  return E_NOTIMPL;
}

HRESULT H264SourceFilter::GetCapabilities(DWORD *pCapabilities)
{
  *pCapabilities = AM_SEEKING_CanSeekAbsolute | AM_SEEKING_CanGetDuration;
  return S_OK;
}

HRESULT H264SourceFilter::GetCurrentPosition(LONGLONG *pCurrent)
{
  *pCurrent = m_pPin->m_iCurrentFrame * m_pPin->m_rtFrameLength;
  return S_OK;
}

HRESULT H264SourceFilter::GetDuration(LONGLONG *pDuration)
{
  *pDuration = m_pPin->m_rtFrameLength * m_vFrames.size();
  return S_OK;
}

HRESULT H264SourceFilter::GetPositions( LONGLONG *pCurrent, LONGLONG *pStop)
{
  *pCurrent = m_pPin->m_iCurrentFrame * m_pPin->m_rtFrameLength;
  *pStop = m_pPin->m_rtFrameLength * m_vFrames.size();
  return S_OK;
}

HRESULT H264SourceFilter::GetPreroll( LONGLONG *pllPreroll )
{
  return E_NOTIMPL;
}

HRESULT H264SourceFilter::GetRate( double *dRate )
{
  return E_NOTIMPL;
}

HRESULT H264SourceFilter::GetStopPosition( LONGLONG *pStop)
{
  *pStop = m_pPin->m_rtFrameLength * m_vFrames.size();
  return S_OK;
}

HRESULT H264SourceFilter::GetTimeFormat( GUID *pFormat )
{
  if (!pFormat) return E_POINTER;
  *pFormat = TIME_FORMAT_MEDIA_TIME;
  return S_OK;
}

HRESULT H264SourceFilter::IsFormatSupported( const GUID *pFormat )
{
  if (!pFormat) return E_POINTER;
  else if (*pFormat == TIME_FORMAT_MEDIA_TIME)
  {
    return S_OK;
  }
  else
  {
    return S_FALSE;
  }
}

HRESULT H264SourceFilter::IsUsingTimeFormat( const GUID *pFormat )
{
  if (*pFormat == TIME_FORMAT_MEDIA_TIME)
  {
    return S_OK;
  }
  else
  {
    return S_FALSE;
  }
}

HRESULT H264SourceFilter::QueryPreferredFormat( GUID *pFormat )
{
  if (!pFormat) return E_POINTER;
  *pFormat = TIME_FORMAT_MEDIA_TIME;
  return S_OK;
}

HRESULT H264SourceFilter::SetPositions(LONGLONG *pCurrent, DWORD dwCurrentFlags, LONGLONG *pStop, DWORD dwStopFlags)
{
  // use pCurrent to calculate the frame
  if (!pCurrent) return E_POINTER;
  // Commenting out this for now: need to handle set stop position
  // for now only support setting start position
  // if (pStop) return E_INVALIDARG;
  
  // only checking for absolute positioning
  if (dwCurrentFlags & AM_SEEKING_AbsolutePositioning)
  {
    // find index of frame
    unsigned uiIndexOfFrame = static_cast<unsigned>((*pCurrent) / m_pPin->m_rtFrameLength);
    if (uiIndexOfFrame >= m_vFrames.size()) return E_INVALIDARG;
    else
    {
      // the next call to readNalUnit will read the frame into the buffer and set all member values correctly
      skipToFrame(uiIndexOfFrame);
      return S_OK;
    }
  }
  else
    return E_INVALIDARG;
}

HRESULT H264SourceFilter::SetRate( double dRate )
{
  return E_NOTIMPL;
}

HRESULT H264SourceFilter::SetTimeFormat( const GUID *pFormat )
{
  return E_NOTIMPL;
}
