// NeuronClient/Unicode.h
#pragma once

#include <string>
#include <string_view>

namespace Neuron
{

/// UTF-8, as NeuronClient's API takes text, to the UTF-16 Windows takes (Design/ADR/ADR-005). An
/// invalid sequence becomes U+FFFD.
[[nodiscard]] std::wstring Utf8ToUtf16(std::string_view _utf8);

/// UTF-16 from Windows back to UTF-8. An unpaired surrogate becomes U+FFFD.
[[nodiscard]] std::string Utf16ToUtf8(std::wstring_view _utf16);

} // namespace Neuron
