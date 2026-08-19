#ifndef FILE_PICKER_RENDERER_HPP
#define FILE_PICKER_RENDERER_HPP

#include "terminal.hpp"

#include <string>
#include <vector>

namespace fp {

class PickerState;

// 三栏渲染引擎：把 PickerState 渲染为一张完整的终端画面（单个字符串）
// 一次性输出，避免闪烁；每行按终端宽度补齐，覆盖式重绘不留残影
// 布局：顶栏(模式/当前目录路径/选中数) / 列标题 / 三栏条目区 / 底部状态栏
class Renderer {
public:
  // 返回完整帧（每行以 CSI 光标定位 \033[row;1H 开头，末尾无换行）
  // 为底部状态栏文本
  std::string render(PickerState &state, Size size,
                     const std::string &status_left,
                     const std::string &status_right);

  // 帮助页：整帧渲染帮助文本（H 键打开）
  // 为滚动偏移，内部夹紧到合法范围
  std::string render_help(const std::vector<std::string> &lines, int &scroll,
                          Size size);
};

} // namespace fp

#endif // FILE_PICKER_RENDERER_HPP
