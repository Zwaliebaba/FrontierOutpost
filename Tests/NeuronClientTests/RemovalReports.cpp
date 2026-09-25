// Tests/NeuronClientTests/RemovalReports.cpp
//
// What the core reports when the device is removed: each command list DRED says the GPU had not
// finished, with the operation it stopped at, and what was at the address of a page fault (plan
// §5.3). A test cannot remove the device to see it: there is one per adapter, and it would stay
// removed for the tests after. These give the report what DRED would.
#include "pch.h"

#include <d3d12.h>

#include "Check.h"
#include "DeviceRemoval.h"

#include <array>
#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

/// Asserts that _report holds _part.
void ExpectPart(const std::string& _report, std::string_view _part)
{
  const std::wstring message = Widen(_part) + L" is not in: " + Widen(_report);
  Assert::IsTrue(_report.find(_part) != std::string::npos, message.c_str());
}

constexpr std::array<D3D12_AUTO_BREADCRUMB_OP, 4> OPERATIONS = {
  D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER, D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW, D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED,
  D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER};

} // namespace

TEST_CLASS(RemovalReports)
{
public:
  TEST_METHOD(NamesTheOperationEachUnfinishedListStoppedAt)
  {
    // One list the GPU finished, one it stopped in after two operations, and one it had not begun.
    const UINT32 all = 4;
    const UINT32 two = 2;
    const UINT32 none = 0;
    const D3D12_AUTO_BREADCRUMB_NODE waiting{"Waiting", nullptr, nullptr, nullptr, nullptr, nullptr, 4, &none, OPERATIONS.data(), nullptr};
    const D3D12_AUTO_BREADCRUMB_NODE stopped{nullptr, L"Stopped", nullptr, L"Queue",          nullptr,
                                             nullptr, 4,          &two,    OPERATIONS.data(), &waiting};
    const D3D12_AUTO_BREADCRUMB_NODE finished{nullptr, L"Finished", nullptr, L"Queue",          nullptr,
                                              nullptr, 4,           &all,    OPERATIONS.data(), &stopped};
    const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{&finished};
    const std::string report = Neuron::DescribeRemoval(DXGI_ERROR_DEVICE_HUNG, &breadcrumbs, nullptr);
    ExpectPart(report, "the device was removed (0x887a0006)");
    ExpectPart(report, "command list \"Stopped\" on queue \"Queue\" had finished 2 of its 4 operations, up to ClearRenderTargetView; "
                       "the next was DrawIndexedInstanced");
    ExpectPart(report, "command list \"Waiting\" on an unnamed queue had finished 0 of its 4 operations; the next was ResourceBarrier");
    Assert::IsTrue(report.find("Finished") == std::string::npos, L"a finished list was reported");
  }

  TEST_METHOD(SaysWhenEveryListWasFinished)
  {
    const UINT32 all = 4;
    const D3D12_AUTO_BREADCRUMB_NODE finished{nullptr, L"Finished", nullptr, L"Queue",          nullptr,
                                              nullptr, 4,           &all,    OPERATIONS.data(), nullptr};
    const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{&finished};
    const D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{0, nullptr, nullptr};
    const std::string report = Neuron::DescribeRemoval(DXGI_ERROR_DEVICE_REMOVED, &breadcrumbs, &pageFault);
    ExpectPart(report, "the GPU had finished every command list it was given");
    ExpectPart(report, "no page fault was recorded");
  }

  TEST_METHOD(NamesWhatWasWhereThePageFaulted)
  {
    const D3D12_DRED_ALLOCATION_NODE heap{nullptr, L"Pool", D3D12_DRED_ALLOCATION_TYPE_HEAP, nullptr};
    const D3D12_DRED_ALLOCATION_NODE glow{nullptr, L"Glow", D3D12_DRED_ALLOCATION_TYPE_RESOURCE, &heap};
    const D3D12_DRED_ALLOCATION_NODE gone{"Gone", nullptr, D3D12_DRED_ALLOCATION_TYPE_RESOURCE, nullptr};
    const D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{0x12340000, &glow, &gone};
    const std::string report = Neuron::DescribeRemoval(DXGI_ERROR_DEVICE_REMOVED, nullptr, &pageFault);
    ExpectPart(report, "a page fault at GPU address 0x0000000012340000");
    ExpectPart(report, "allocated there: \"Glow\", a resource; \"Pool\", a heap");
    ExpectPart(report, "freed from there not long before: \"Gone\", a resource");
  }

  TEST_METHOD(SaysWhenDredRecordedNothing)
  {
    const std::string report = Neuron::DescribeRemoval(DXGI_ERROR_DEVICE_RESET, nullptr, nullptr);
    ExpectPart(report, "the device was removed (0x887a0007)");
    ExpectPart(report, "DRED: nothing was recorded");
  }
};

} // namespace NeuronClientTests
