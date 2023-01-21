/*
 * @文件作用: Lua独立解释器
 * @功能分类: 可执行的解析器，字节码编译器
 * @注释者: frog-game
 * @LastEditTime: 2023-01-21 20:59:27
 */

// lua.hpp
// Lua header files for C++
// <<extern "C">> not supplied automatically because Lua also compiles as C++

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
