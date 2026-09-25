// NeuronClient/ImageFile.cpp
#include "pch.h"

#include <objbase.h>
#include <wincodec.h>

#include "ImageFile.h"

#include <climits>
#include <cstring>
#include <format>
#include <utility>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

constexpr std::size_t BYTES_PER_PIXEL = 4;

/// COM on the calling thread for as long as this lives. A thread that already has COM, in either
/// apartment, keeps it as it is.
class ComScope
{
public:
  ComScope() noexcept
    : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
  {
  }

  ~ComScope()
  {
    if (SUCCEEDED(m_result))
    {
      CoUninitialize();
    }
  }

  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

private:
  HRESULT m_result;
};

bool Succeeded(HRESULT _result, const char* _step, std::string& _error)
{
  if (SUCCEEDED(_result))
  {
    return true;
  }
  _error = std::format("WIC: {} failed with 0x{:08X}", _step, static_cast<unsigned long>(_result));
  return false;
}

ComPtr<IWICImagingFactory> CreateFactory(std::string& _error)
{
  ComPtr<IWICImagingFactory> factory;
  Succeeded(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
            "creating the imaging factory", _error);
  return factory;
}

/// WIC takes the pixels it reads through pointers that are not const, so it reads a copy.
std::vector<BYTE> Copy(std::span<const std::byte> _bytes)
{
  std::vector<BYTE> copy(_bytes.size());
  if (!_bytes.empty())
  {
    std::memcpy(copy.data(), _bytes.data(), _bytes.size());
  }
  return copy;
}

} // namespace

bool ImageFile::Decode(std::span<const std::byte> _fileBytes, ImageFile& _outImage, std::string& _error)
{
  if (_fileBytes.size() > MAXDWORD)
  {
    _error = "WIC: the file is too large";
    return false;
  }
  const ComScope com;
  const ComPtr<IWICImagingFactory> factory = CreateFactory(_error);
  if (!factory)
  {
    return false;
  }

  std::vector<BYTE> file = Copy(_fileBytes);
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> rgba;
  if (!Succeeded(factory->CreateStream(&stream), "creating a stream", _error) ||
      !Succeeded(stream->InitializeFromMemory(file.data(), static_cast<DWORD>(file.size())), "reading the file", _error) ||
      !Succeeded(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder),
                 "recognizing the format", _error) ||
      !Succeeded(decoder->GetFrame(0, &frame), "decoding the first frame", _error) ||
      !Succeeded(factory->CreateFormatConverter(&rgba), "creating a converter", _error) ||
      !Succeeded(
        rgba->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom),
        "converting to RGBA8", _error))
  {
    return false;
  }

  UINT widthPixels = 0;
  UINT heightPixels = 0;
  if (!Succeeded(rgba->GetSize(&widthPixels, &heightPixels), "reading the size", _error))
  {
    return false;
  }
  const std::size_t strideBytes = static_cast<std::size_t>(widthPixels) * BYTES_PER_PIXEL;
  const std::size_t sizeBytes = strideBytes * heightPixels;
  if (sizeBytes > UINT_MAX)
  {
    _error = "WIC: the image is too large";
    return false;
  }
  std::vector<std::byte> pixels(sizeBytes);
  if (!Succeeded(
        rgba->CopyPixels(nullptr, static_cast<UINT>(strideBytes), static_cast<UINT>(sizeBytes), reinterpret_cast<BYTE*>(pixels.data())),
        "copying the pixels", _error))
  {
    return false;
  }

  _outImage.m_widthPixels = widthPixels;
  _outImage.m_heightPixels = heightPixels;
  _outImage.m_pixels = std::move(pixels);
  return true;
}

bool ImageFile::EncodePng(std::uint32_t _widthPixels, std::uint32_t _heightPixels, std::span<const std::byte> _rgba,
                          std::vector<std::byte>& _outFileBytes, std::string& _error)
{
  const std::size_t strideBytes = static_cast<std::size_t>(_widthPixels) * BYTES_PER_PIXEL;
  const std::size_t sizeBytes = strideBytes * _heightPixels;
  if (_widthPixels == 0 || _heightPixels == 0 || _rgba.size() != sizeBytes)
  {
    _error = std::format("WIC: {} bytes of pixels for a {}x{} image, which needs {}", _rgba.size(), _widthPixels, _heightPixels, sizeBytes);
    return false;
  }
  if (sizeBytes > UINT_MAX)
  {
    _error = "WIC: the image is too large";
    return false;
  }
  const ComScope com;
  const ComPtr<IWICImagingFactory> factory = CreateFactory(_error);
  if (!factory)
  {
    return false;
  }

  ComPtr<IStream> memory;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> options;
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
  if (!Succeeded(CreateStreamOnHGlobal(nullptr, TRUE, &memory), "creating a stream", _error) ||
      !Succeeded(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "creating the PNG encoder", _error) ||
      !Succeeded(encoder->Initialize(memory.Get(), WICBitmapEncoderNoCache), "starting the file", _error) ||
      !Succeeded(encoder->CreateNewFrame(&frame, &options), "creating the frame", _error) ||
      !Succeeded(frame->Initialize(options.Get()), "starting the frame", _error) ||
      !Succeeded(frame->SetSize(_widthPixels, _heightPixels), "setting the size", _error) ||
      !Succeeded(frame->SetPixelFormat(&format), "setting the pixel format", _error))
  {
    return false;
  }

  std::vector<BYTE> pixels = Copy(_rgba);
  if (format == GUID_WICPixelFormat32bppRGBA)
  {
    if (!Succeeded(frame->WritePixels(_heightPixels, static_cast<UINT>(strideBytes), static_cast<UINT>(sizeBytes), pixels.data()),
                   "writing the pixels", _error))
    {
      return false;
    }
  }
  else
  {
    // The encoder asked for another layout: convert to it.
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICFormatConverter> converted;
    if (!Succeeded(factory->CreateBitmapFromMemory(_widthPixels, _heightPixels, GUID_WICPixelFormat32bppRGBA,
                                                   static_cast<UINT>(strideBytes), static_cast<UINT>(sizeBytes), pixels.data(), &bitmap),
                   "wrapping the pixels", _error) ||
        !Succeeded(factory->CreateFormatConverter(&converted), "creating a converter", _error) ||
        !Succeeded(converted->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom),
                   "converting for the encoder", _error) ||
        !Succeeded(frame->WriteSource(converted.Get(), nullptr), "writing the pixels", _error))
    {
      return false;
    }
  }

  STATSTG stat{};
  LARGE_INTEGER start{};
  if (!Succeeded(frame->Commit(), "finishing the frame", _error) || !Succeeded(encoder->Commit(), "finishing the file", _error) ||
      !Succeeded(memory->Stat(&stat, STATFLAG_NONAME), "measuring the file", _error) ||
      !Succeeded(memory->Seek(start, STREAM_SEEK_SET, nullptr), "rewinding the file", _error))
  {
    return false;
  }
  if (stat.cbSize.QuadPart > ULONG_MAX)
  {
    _error = "WIC: the file is too large";
    return false;
  }
  std::vector<std::byte> file(static_cast<std::size_t>(stat.cbSize.QuadPart));
  ULONG readBytes = 0;
  if (!Succeeded(memory->Read(file.data(), static_cast<ULONG>(file.size()), &readBytes), "reading the file back", _error))
  {
    return false;
  }
  if (readBytes != file.size())
  {
    _error = std::format("WIC: read {} of the file's {} bytes back", readBytes, file.size());
    return false;
  }
  _outFileBytes = std::move(file);
  return true;
}

} // namespace Neuron
