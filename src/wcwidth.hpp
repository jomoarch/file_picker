#ifndef FILE_PICKER_WCWIDTH_HPP
#define FILE_PICKER_WCWIDTH_HPP

#include <cstdint>

namespace fp {

// 返回值：-1=控制字符，0=组合字符/零宽，1=半角，2=全角
int mk_wcwidth(uint32_t ucs);

} // namespace fp

#endif // FILE_PICKER_WCWIDTH_HPP