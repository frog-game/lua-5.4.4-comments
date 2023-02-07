/*
 * @文件作用: c库编写用到的辅助函数库
 * @功能分类: 内嵌库
 * @注释者: frog-game
 * @LastEditTime: 2023-02-07 15:52:29
 */
/*
** $Id: lauxlib.c $
** Auxiliary functions for building Lua libraries
** See Copyright Notice in lua.h
*/

#define lauxlib_c
#define LUA_LIB

#include "lprefix.h"


#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
** This file uses only the official API of Lua.
** Any function declared here could be written as an application function.
*/

#include "lua.h"

#include "lauxlib.h"


#if !defined(MAX_SIZET)
/* maximum value for size_t */
/// @brief 最大值 其实就是把0取反在强转然后里面的二进制就全是1了
#define MAX_SIZET	((size_t)(~(size_t)0)) 
#endif


/*
** {======================================================
** Traceback
** =======================================================
*/

/// @brief 堆栈第一部分大小 用来打印堆栈信息的
#define LEVELS1	10	/* size of the first part of the stack */

/// @brief 堆栈第二部分大小 用来打印堆栈信息的
#define LEVELS2	11	/* size of the second part of the stack */



/*
** Search for 'objidx' in table at index -1. ('objidx' must be an
** absolute index.) Return 1 + string at top if it found a good name.
*/

/// @brief 查找字段
/// @param L 
/// @param objidx 必须是个绝对值索引
/// @param level 递归层次
/// @return 
static int findfield (lua_State *L, int objidx, int level) {
  if (level == 0 || !lua_istable(L, -1))  //如果level等于0或者栈顶不是table
    return 0;  /* not found */// 没找到
  lua_pushnil(L);  /* start 'next' loop *///塞入一个nil是为了下面的循环
    /*此时栈的状态
    -------
    | -1 nil
    | -2 lib_table
    -------
    */
  while (lua_next(L, -2)) {  /* for each pair in table *///循环这个table
        /*此时栈的状态
        -------
        | -1 value
        | -2 key
        | -3 lib_table
        -------
        */

    if (lua_type(L, -2) == LUA_TSTRING) {  /* ignore non-string keys *///是字符串
      if (lua_rawequal(L, objidx, -1)) {  /* found object? *///如果找到了
        /*此时栈的状态
        -------
        | -1 value
        | -2 key
        | -3 lib_table
        -------
        */
        lua_pop(L, 1);  /* remove value (but keep name) */
          /*此时栈的状态
          -------
          | -1 key
          | -2 lib_table
          -------
          */
        return 1;
      }
      else if (findfield(L, objidx, level - 1)) {  /* try recursively *///进行递归
        /* stack: lib_name, lib_table, field_name (top) */

        /*此时栈的状态
        //  -------
        // | -1 field_name
        // | -2 lib_table
        // | -3 lib_name
        // -------
        */
        lua_pushliteral(L, ".");  /* place '.' between the two names */
        /*此时栈的状态
        -------
        | -1 "." 
        | -2 field_name
        | -3 lib_table
        | -4 lib_name
        -------
        */
        lua_replace(L, -3);  /* (in the slot occupied by table) */
        /*此时栈的状态
        -------
        | -1 field_name
        | -2 "." 
        | -3 lib_name
        -------
        */
        lua_concat(L, 3);  /* lib_name.field_name */
        return 1;
      }
    }

        /*此时栈的状态
        -------
        | -1 value
        | -2 key
        | -3 lib_table
        -------
        */
    lua_pop(L, 1);  /* remove value */

     /*此时栈的状态
    -------
    | -1 key
    | -2 lib_table
    -------
    */
  }
  return 0;  /* not found */
}


/*
** Search for a name for a function in all loaded modules
*/

/// @brief 在加载的modules中搜索函数的名称
/// @param L 
/// @param ar 
/// @return 
static int pushglobalfuncname (lua_State *L, lua_Debug *ar) {
  int top = lua_gettop(L);//得到栈顶索引
  lua_getinfo(L, "f", ar);  /* push function *///把正在运行中指定级别处函数压入堆栈
  lua_getfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);//如果是全局注册表，则idx=LUA_REGISTRYINDEX；如果是在栈上的一个Table表，则数字索引 从全局注册表上找LUA_LOADED_TABLE，放置到L->top
  if (findfield(L, top + 1, 2)) {//查找字段名字
    const char *name = lua_tostring(L, -1);//把字段名字取出来
    if (strncmp(name, LUA_GNAME ".", 3) == 0) {  /* name start with '_G.'? *///形如_G.??? 这种类型的名字
      lua_pushstring(L, name + 3);  /* push name without prefix *///把_G.??? 后缀压栈（也就是???）
      lua_remove(L, -2);  /* remove original name */// 移除掉原始名字 也就是_G.???
    }
    lua_copy(L, -1, top + 1);  /* copy name to proper place *////将名称复制到正确的位置
    lua_settop(L, top + 1);  /* remove table "loaded" and name copy *//// 放到这个位置的时候其实之前的loaded和name copy就给移除掉了
    return 1;
  }
  else {
    lua_settop(L, top);  /* remove function and global table *///移除掉之前压入的function和table
    return 0;
  }
}

/// @brief 塞入一个函数相关的信息
/// @param L 
/// @param ar 
static void pushfuncname (lua_State *L, lua_Debug *ar) {
  if (pushglobalfuncname(L, ar)) {  /* try first a global name */// 尝试塞入到全局表中
    lua_pushfstring(L, "function '%s'", lua_tostring(L, -1)); /// 如果有塞入拼接的字符串信息到栈顶
    lua_remove(L, -2);  /* remove name *///删除名字
  }
  else if (*ar->namewhat != '\0')  /* is there a name from code? *///namewhat是不是有值
    lua_pushfstring(L, "%s '%s'", ar->namewhat, ar->name);  /* use it *///如果有塞入拼接的字符串信息到栈顶
  else if (*ar->what == 'm')  /* main? *///是main
      lua_pushliteral(L, "main chunk"); //塞入main chunk到栈顶
  else if (*ar->what != 'C')  /* for Lua functions, use <file:line> *///是c函数
    lua_pushfstring(L, "function <%s:%d>", ar->short_src, ar->linedefined);//塞入拼接的字符串信息到栈顶
  else  /* nothing left... *///其他
    lua_pushliteral(L, "?");//塞入到栈顶
}


/// @brief 找到函数第一次调用位置
/// @param L 
/// @return 
static int lastlevel (lua_State *L) {
  lua_Debug ar;
  int li = 1, le = 1;
  /* find an upper bound *///查找上限
  while (lua_getstack(L, le, &ar)) { li = le; le *= 2; }
  /* do a binary search *///二分查找
  while (li < le) {
    int m = (li + le)/2;
    if (lua_getstack(L, m, &ar)) li = m + 1;
    else le = m;
  }
  return le - 1;
}

/// @brief 打印堆栈信息用的
/// @param L 
/// @param L1 
/// @param msg 
/// @param level 
/// @return 
LUALIB_API void luaL_traceback (lua_State *L, lua_State *L1,
                                const char *msg, int level) {
  luaL_Buffer b;
  lua_Debug ar;
  int last = lastlevel(L1);//找到第一层调用位置
  int limit2show = (last - level > LEVELS1 + LEVELS2) ? LEVELS1 : -1;
  luaL_buffinit(L, &b);
  if (msg) {
    luaL_addstring(&b, msg);
    luaL_addchar(&b, '\n');
  }
  luaL_addstring(&b, "stack traceback:");
  while (lua_getstack(L1, level++, &ar)) {
    if (limit2show-- == 0) {  /* too many levels? */
      int n = last - level - LEVELS2 + 1;  /* number of levels to skip */
      lua_pushfstring(L, "\n\t...\t(skipping %d levels)", n);
      luaL_addvalue(&b);  /* add warning about skip */
      level += n;  /* and skip to last levels */
    }
    else {
      lua_getinfo(L1, "Slnt", &ar);
      if (ar.currentline <= 0)
        lua_pushfstring(L, "\n\t%s: in ", ar.short_src);
      else
        lua_pushfstring(L, "\n\t%s:%d: in ", ar.short_src, ar.currentline);
      luaL_addvalue(&b);
      pushfuncname(L, &ar);
      luaL_addvalue(&b);
      if (ar.istailcall)
        luaL_addstring(&b, "\n\t(...tail calls...)");
    }
  }
  luaL_pushresult(&b);
}

/* }====================================================== */


/*
** {======================================================
** Error-report functions
** =======================================================
*/

/// @brief 抛出一个参数错误
/// @param L 
/// @param arg 
/// @param extramsg 
/// @return 
LUALIB_API int luaL_argerror (lua_State *L, int arg, const char *extramsg) {
  lua_Debug ar;
  if (!lua_getstack(L, 0, &ar))  /* no stack frame? */
    return luaL_error(L, "bad argument #%d (%s)", arg, extramsg);
  lua_getinfo(L, "n", &ar);
  if (strcmp(ar.namewhat, "method") == 0) {
    arg--;  /* do not count 'self' */
    if (arg == 0)  /* error is in the self argument itself? */
      return luaL_error(L, "calling '%s' on bad self (%s)",
                           ar.name, extramsg);
  }
  if (ar.name == NULL)
    ar.name = (pushglobalfuncname(L, &ar)) ? lua_tostring(L, -1) : "?";
  return luaL_error(L, "bad argument #%d to '%s' (%s)",
                        arg, ar.name, extramsg);
}

/// @brief 抛出一个类型错误
/// @param L 
/// @param arg 
/// @param tname 
/// @return 
LUALIB_API int luaL_typeerror (lua_State *L, int arg, const char *tname) {
  const char *msg;
  const char *typearg;  /* name for the type of the actual argument */
  if (luaL_getmetafield(L, arg, "__name") == LUA_TSTRING)
    typearg = lua_tostring(L, -1);  /* use the given type name */
  else if (lua_type(L, arg) == LUA_TLIGHTUSERDATA)
    typearg = "light userdata";  /* special name for messages */
  else
    typearg = luaL_typename(L, arg);  /* standard name */
  msg = lua_pushfstring(L, "%s expected, got %s", tname, typearg);
  return luaL_argerror(L, arg, msg);
}

/// @brief 对luaL_typeerror的一层包装
/// @param L 
/// @param arg 
/// @param tag 
static void tag_error (lua_State *L, int arg, int tag) {
  luaL_typeerror(L, arg, lua_typename(L, tag));
}


/*
** The use of 'lua_pushfstring' ensures this function does not
** need reserved stack space when called.
*/

/// @brief 构建错误消息的前缀
/// @param L 
/// @param level 
/// @return 
LUALIB_API void luaL_where (lua_State *L, int level) {
  lua_Debug ar;
  if (lua_getstack(L, level, &ar)) {  /* check function at level */
    lua_getinfo(L, "Sl", &ar);  /* get info about it */
    if (ar.currentline > 0) {  /* is there info? */
      lua_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
      return;
    }
  }
  lua_pushfstring(L, "");  /* else, no information available... */
}


/*
** Again, the use of 'lua_pushvfstring' ensures this function does
** not need reserved stack space when called. (At worst, it generates
** an error with "stack overflow" instead of the given message.)
*/

/// @brief 抛出一个错误。错误消息的格式由 fmt 给出。后面需提供若干参数
/// @param L 
/// @param fmt 
/// @param  
/// @return 
LUALIB_API int luaL_error (lua_State *L, const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  luaL_where(L, 1);
  lua_pushvfstring(L, fmt, argp);
  va_end(argp);
  lua_concat(L, 2);
  return lua_error(L);
}

/// @brief 这个函数用于生成标准库中和文件相关的函数的返回值。（指 (io.open，os.rename，file:seek，等。)
/// @param L 
/// @param stat 
/// @param fname 
/// @return 
LUALIB_API int luaL_fileresult (lua_State *L, int stat, const char *fname) {
  int en = errno;  /* calls to Lua API may change this value */
  if (stat) {
    lua_pushboolean(L, 1);
    return 1;
  }
  else {
    luaL_pushfail(L);
    if (fname)
      lua_pushfstring(L, "%s: %s", fname, strerror(en));
    else
      lua_pushstring(L, strerror(en));
    lua_pushinteger(L, en);
    return 3;
  }
}


#if !defined(l_inspectstat)	/* { */

#if defined(LUA_USE_POSIX)

#include <sys/wait.h>

/*
** use appropriate macros to interpret 'pclose' return status
*/
#define l_inspectstat(stat,what)  \
   if (WIFEXITED(stat)) { stat = WEXITSTATUS(stat); } \
   else if (WIFSIGNALED(stat)) { stat = WTERMSIG(stat); what = "signal"; }

#else

#define l_inspectstat(stat,what)  /* no op */

#endif

#endif				/* } */

/// @brief 这个函数用于生成标准库中和进程相关函数的返回值。（指 os.execute 和 io.close）
/// @param L 
/// @param stat 
/// @return 
LUALIB_API int luaL_execresult (lua_State *L, int stat) {
  if (stat != 0 && errno != 0)  /* error with an 'errno'? */
    return luaL_fileresult(L, 0, NULL);
  else {
    const char *what = "exit";  /* type of termination */
    l_inspectstat(stat, what);  /* interpret result */
    if (*what == 'e' && stat == 0)  /* successful termination? */
      lua_pushboolean(L, 1);
    else
      luaL_pushfail(L);
    lua_pushstring(L, what);
    lua_pushinteger(L, stat);
    return 3;  /* return true/fail,what,code */
  }
}

/* }====================================================== */



/*
** {======================================================
** Userdata's metatable manipulation
** =======================================================
*/

/// @brief 如果注册表中已经有名为tname的key,则返回0. 
// 否则创建一个新table作为userdata的元表. 这个元表存储在注册表中,并以tname为key. 返回1. 
// 函数完成后将该元表置于栈顶. 
/// @param L 
/// @param tname 
/// @return 
LUALIB_API int luaL_newmetatable (lua_State *L, const char *tname) {
  if (luaL_getmetatable(L, tname) != LUA_TNIL)  /* name already in use? */
    return 0;  /* leave previous value on top, but return 0 */
  lua_pop(L, 1);
  lua_createtable(L, 0, 2);  /* create metatable */
  lua_pushstring(L, tname);
  lua_setfield(L, -2, "__name");  /* metatable.__name = tname */
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, tname);  /* registry.name = metatable */
  return 1;
}

/// @brief 将栈顶元素存储到注册表中, 它的key为tname
/// @param L 
/// @param tname 
/// @return 
LUALIB_API void luaL_setmetatable (lua_State *L, const char *tname) {
  luaL_getmetatable(L, tname);
  lua_setmetatable(L, -2);
}

/// @brief 此函数和 luaL_checkudata 类似。但它在测试失败时会返回 NULL 而不是抛出错误
/// @param L 
/// @param ud 
/// @param tname 
/// @return 
LUALIB_API void *luaL_testudata (lua_State *L, int ud, const char *tname) {
  void *p = lua_touserdata(L, ud);
  if (p != NULL) {  /* value is a userdata? */
    if (lua_getmetatable(L, ud)) {  /* does it have a metatable? */
      luaL_getmetatable(L, tname);  /* get correct metatable */
      if (!lua_rawequal(L, -1, -2))  /* not the same? */
        p = NULL;  /* value is a userdata with wrong metatable */
      lua_pop(L, 2);  /* remove both metatables */
      return p;
    }
  }
  return NULL;  /* value is not a userdata with a metatable */
}

/// @brief 检查函数的第 arg 个参数是否是一个类型为tname 的用户数据 它会返回该用户数据的地址
/// @param L 
/// @param ud 
/// @param tname 
/// @return 
LUALIB_API void *luaL_checkudata (lua_State *L, int ud, const char *tname) {
  void *p = luaL_testudata(L, ud, tname);
  luaL_argexpected(L, p != NULL, ud, tname);
  return p;
}

/* }====================================================== */


/*
** {======================================================
** Argument check functions
** =======================================================
*/

/// @brief 检查函数的第 arg 个参数是否是一个字符串，并在数组 lst （比如是零结尾的字符串数组）中查找这个字符串。
// 返回匹配到的字符串在数组中的索引号。如果参数不是字符串，或是字符串在数组中匹配不到，都将抛出错误。
// 如果 def 不为 NULL，函数就把 def 当作默认值。默认值在参数 arg 不存在，或该参数是 nil 时生效。
// 这个函数通常用于将字符串映射为 C 枚举量。（在 Lua 库中做这个转换可以让其使用字符串，而不是数字来做一些选项。）
/// @param L 
/// @param arg 
/// @param def 
/// @param lst 
/// @return 
LUALIB_API int luaL_checkoption (lua_State *L, int arg, const char *def,
                                 const char *const lst[]) {
  const char *name = (def) ? luaL_optstring(L, arg, def) :
                             luaL_checkstring(L, arg);
  int i;
  for (i=0; lst[i]; i++)
    if (strcmp(lst[i], name) == 0)
      return i;
  return luaL_argerror(L, arg,
                       lua_pushfstring(L, "invalid option '%s'", name));
}


/*
** Ensures the stack has at least 'space' extra slots, raising an error
** if it cannot fulfill the request. (The error handling needs a few
** extra slots to format the error message. In case of an error without
** this extra space, Lua will generate the same 'stack overflow' error,
** but without 'msg'.)
*/

/// @brief 检查栈是否溢出
/// @param L 
/// @param space 
/// @param msg 
/// @return 
LUALIB_API void luaL_checkstack (lua_State *L, int space, const char *msg) {
  if (l_unlikely(!lua_checkstack(L, space))) {
    if (msg)
      luaL_error(L, "stack overflow (%s)", msg);
    else
      luaL_error(L, "stack overflow");
  }
}

/// @brief 检查函数的第 arg 个参数的类型是否是 t
/// @param L 
/// @param arg 
/// @param t 
/// @return 
LUALIB_API void luaL_checktype (lua_State *L, int arg, int t) {
  if (l_unlikely(lua_type(L, arg) != t))
    tag_error(L, arg, t);
}

/// @brief 检查函数在 arg 位置是否有任何类型（包括 nil）的参数。
/// @param L 
/// @param arg 
/// @return 
LUALIB_API void luaL_checkany (lua_State *L, int arg) {
  if (l_unlikely(lua_type(L, arg) == LUA_TNONE))
    luaL_argerror(L, arg, "value expected");
}

/// @brief 检查函数的第 arg 个参数是否是一个字符串，并返回该字符串；如果 l 不为 NULL ，将字符串的长度填入 *l。
// 这个函数使用 lua_tolstring 来获取结果。所以该函数有可能引发的转换都同样有效
/// @param L 
/// @param arg 
/// @param len 
/// @return 
LUALIB_API const char *luaL_checklstring (lua_State *L, int arg, size_t *len) {
  const char *s = lua_tolstring(L, arg, len);
  if (l_unlikely(!s)) tag_error(L, arg, LUA_TSTRING);
  return s;
}

/// @brief 如果函数的第 arg 个参数是一个字符串，返回该字符串。若该参数不存在或是 nil，返回 d。除此之外的情况，抛出错误。
// 若 l 不为 NULL，将结果的长度填入 *l
/// @param L 
/// @param arg 
/// @param def 
/// @param len 
/// @return 
LUALIB_API const char *luaL_optlstring (lua_State *L, int arg,
                                        const char *def, size_t *len) {
  if (lua_isnoneornil(L, arg)) {
    if (len)
      *len = (def ? strlen(def) : 0);
    return def;
  }
  else return luaL_checklstring(L, arg, len);
}

/// @brief 检查函数的第 arg 个参数是否是一个数字，并返回这个数字
/// @param L 
/// @param arg 
/// @return 
LUALIB_API lua_Number luaL_checknumber (lua_State *L, int arg) {
  int isnum;
  lua_Number d = lua_tonumberx(L, arg, &isnum);
  if (l_unlikely(!isnum))
    tag_error(L, arg, LUA_TNUMBER);
  return d;
}

/// @brief 如果函数的第 arg 个参数是一个数字，返回该数字。若该参数不存在或是 nil，返回 d。除此之外的情况，抛出错误
/// @param L 
/// @param arg 
/// @param def 
/// @return 
LUALIB_API lua_Number luaL_optnumber (lua_State *L, int arg, lua_Number def) {
  return luaL_opt(L, luaL_checknumber, arg, def);
}

/// @brief integer 错误提示
/// @param L 
/// @param arg 
static void interror (lua_State *L, int arg) {
  if (lua_isnumber(L, arg))
    luaL_argerror(L, arg, "number has no integer representation");
  else
    tag_error(L, arg, LUA_TNUMBER);
}


/// @brief 检查函数的第 arg 个参数是否是一个整型，并返回这个整型
/// @param L 
/// @param arg 
/// @return 
LUALIB_API lua_Integer luaL_checkinteger (lua_State *L, int arg) {
  int isnum;
  lua_Integer d = lua_tointegerx(L, arg, &isnum);
  if (l_unlikely(!isnum)) {
    interror(L, arg);
  }
  return d;
}

/// @brief 如果函数的第 arg 个参数是一个整数（或可以转换为一个整数），返回该整数。若该参数不存在或是 nil，返回 d。除此之外的情况，抛出错误
/// @param L 
/// @param arg 
/// @param def 
/// @return 
LUALIB_API lua_Integer luaL_optinteger (lua_State *L, int arg,
                                                      lua_Integer def) {
  return luaL_opt(L, luaL_checkinteger, arg, def);
}

/* }====================================================== */


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

/* userdata to box arbitrary data */
typedef struct UBox {//userdata 缓存的地方
  void *box;
  size_t bsize;
} UBox;


/// @brief 调整box大小
/// @param L 
/// @param idx 
/// @param newsize 
/// @return 
static void *resizebox (lua_State *L, int idx, size_t newsize) {
  void *ud;
  lua_Alloc allocf = lua_getallocf(L, &ud);
  UBox *box = (UBox *)lua_touserdata(L, idx);
  void *temp = allocf(ud, box->box, box->bsize, newsize);
  if (l_unlikely(temp == NULL && newsize > 0)) {  /* allocation error? */
    lua_pushliteral(L, "not enough memory");
    lua_error(L);  /* raise a memory error */
  }
  box->box = temp;
  box->bsize = newsize;
  return temp;
}

/// @brief 对box进行gc调整
/// @param L 
/// @return 
static int boxgc (lua_State *L) {
  resizebox(L, 1, 0);
  return 0;
}

/// @brief box元方法
static const luaL_Reg boxmt[] = {  /* box metamethods */
  {"__gc", boxgc},
  {"__close", boxgc},
  {NULL, NULL}
};

/// @brief new一个box
/// @param L 
static void newbox (lua_State *L) {
  UBox *box = (UBox *)lua_newuserdatauv(L, sizeof(UBox), 0);
  box->box = NULL;
  box->bsize = 0;
  if (luaL_newmetatable(L, "_UBOX*"))  /* creating metatable? */
    luaL_setfuncs(L, boxmt, 0);  /* set its metamethods */
  lua_setmetatable(L, -2);
}


/*
** check whether buffer is using a userdata on the stack as a temporary
** buffer
*/

/// @brief 检查缓冲区是否正在使用堆栈上的用户数据作为临时缓冲区
#define buffonstack(B)	((B)->b != (B)->init.b)


/*
** Whenever buffer is accessed, slot 'idx' must either be a box (which
** cannot be NULL) or it is a placeholder for the buffer.
*/

/// @brief 每当访问缓冲区时，idx指定的数据必须是一个box（不能为 NULL），或者是缓冲区的占位符
#define checkbufferlevel(B,idx)  \
  lua_assert(buffonstack(B) ? lua_touserdata(B->L, idx) != NULL  \
                            : lua_touserdata(B->L, idx) == (void*)B)


/*
** Compute new size for buffer 'B', enough to accommodate extra 'sz'
** bytes.
*/

/// @brief 计算缓冲区B的新大小，足以容纳额外的sz字节
/// @param B 
/// @param sz 
/// @return 
static size_t newbuffsize (luaL_Buffer *B, size_t sz) {
  size_t newsize = B->size * 2;  /* double buffer size */
  if (l_unlikely(MAX_SIZET - sz < B->n))  /* overflow in (B->n + sz)? */
    return luaL_error(B->L, "buffer too large");
  if (newsize < B->n + sz)  /* double is not big enough? */
    newsize = B->n + sz;
  return newsize;
}


/*
** Returns a pointer to a free area with at least 'sz' bytes in buffer
** 'B'. 'boxidx' is the relative position in the stack where is the
** buffer's box or its placeholder.
*/

/// @brief 返回指向缓冲区中至少具有sz字节的可用区域的指针B。boxidx是堆栈中的相对位置，其中缓冲区的box或其占位符。
/// @param B 
/// @param sz 
/// @param boxidx 
/// @return 
static char *prepbuffsize (luaL_Buffer *B, size_t sz, int boxidx) {
  checkbufferlevel(B, boxidx);
  if (B->size - B->n >= sz)  /* enough space? */
    return B->b + B->n;
  else {
    lua_State *L = B->L;
    char *newbuff;
    size_t newsize = newbuffsize(B, sz);
    /* create larger buffer */
    if (buffonstack(B))  /* buffer already has a box? */
      newbuff = (char *)resizebox(L, boxidx, newsize);  /* resize it */
    else {  /* no box yet */
      lua_remove(L, boxidx);  /* remove placeholder */
      newbox(L);  /* create a new box */
      lua_insert(L, boxidx);  /* move box to its intended position */
      lua_toclose(L, boxidx);
      newbuff = (char *)resizebox(L, boxidx, newsize);
      memcpy(newbuff, B->b, B->n * sizeof(char));  /* copy original content */
    }
    B->b = newbuff;
    B->size = newsize;
    return newbuff + B->n;
  }
}

/*
** returns a pointer to a free area with at least 'sz' bytes
*/

/// @brief 返回一段大小为 sz 的空间地址。你可以将字符串复制其中以加到缓存 B 内
// 将字符串复制其中后，你必须调用 luaL_addsize传入字符串的大小，才会真正把它加入缓存。
/// @param B 
/// @param sz 
/// @return 
LUALIB_API char *luaL_prepbuffsize (luaL_Buffer *B, size_t sz) {
  return prepbuffsize(B, sz, -1);
}

/// @brief 向缓存 B 添加一个长度为 l 的字符串 s。这个字符串可以包含零。
/// @param B 
/// @param s 
/// @param l 
/// @return 
LUALIB_API void luaL_addlstring (luaL_Buffer *B, const char *s, size_t l) {
  if (l > 0) {  /* avoid 'memcpy' when 's' can be NULL */
    char *b = prepbuffsize(B, l, -1);
    memcpy(b, s, l * sizeof(char));
    luaL_addsize(B, l);
  }
}

/// @brief 向缓存 B 添加一个零结尾的字符串 s
/// @param B 
/// @param s 
/// @return 
LUALIB_API void luaL_addstring (luaL_Buffer *B, const char *s) {
  luaL_addlstring(B, s, strlen(s));
}

/// @brief 结束对缓存 B 的使用，将最终的字符串留在栈顶
/// @param B 
/// @return 
LUALIB_API void luaL_pushresult (luaL_Buffer *B) {
  lua_State *L = B->L;
  checkbufferlevel(B, -1);
  lua_pushlstring(L, B->b, B->n);
  if (buffonstack(B))
    lua_closeslot(L, -2);  /* close the box */
  lua_remove(L, -2);  /* remove box or placeholder from the stack */
}

/// @brief 等价于 luaL_addsize，luaL_pushresul
/// @param B 
/// @param sz 
/// @return 
LUALIB_API void luaL_pushresultsize (luaL_Buffer *B, size_t sz) {
  luaL_addsize(B, sz);
  luaL_pushresult(B);
}


/*
** 'luaL_addvalue' is the only function in the Buffer system where the
** box (if existent) is not on the top of the stack. So, instead of
** calling 'luaL_addlstring', it replicates the code using -2 as the
** last argument to 'prepbuffsize', signaling that the box is (or will
** be) bellow the string being added to the buffer. (Box creation can
** trigger an emergency GC, so we should not remove the string from the
** stack before we have the space guaranteed.)
*/

/// @brief 向缓存 B 添加栈顶的一个值，随后将其弹出。
// 这个函数是操作字符串缓存的函数中，唯一一个会（且必须）在栈上放置额外元素的。这个元素将被加入缓存
/// @param B 
/// @return 
LUALIB_API void luaL_addvalue (luaL_Buffer *B) {
  lua_State *L = B->L;
  size_t len;
  const char *s = lua_tolstring(L, -1, &len);
  char *b = prepbuffsize(B, len, -2);
  memcpy(b, s, len * sizeof(char));
  luaL_addsize(B, len);
  lua_pop(L, 1);  /* pop string */
}

/// @brief 初始化缓存 B。这个函数不会分配任何空间；缓存必须以一个变量的形式声明
/// @param L 
/// @param B 
/// @return 
LUALIB_API void luaL_buffinit (lua_State *L, luaL_Buffer *B) {
  B->L = L;
  B->b = B->init.b;
  B->n = 0;
  B->size = LUAL_BUFFERSIZE;
  lua_pushlightuserdata(L, (void*)B);  /* push placeholder */
}

/// @brief 等价于调用序列luaL_buffinit，luaL_prepbuffsize
/// @param L 
/// @param B 
/// @param sz 
/// @return 
LUALIB_API char *luaL_buffinitsize (lua_State *L, luaL_Buffer *B, size_t sz) {
  luaL_buffinit(L, B);
  return prepbuffsize(B, sz, -1);
}

/* }====================================================== */


/*
** {======================================================
** Reference system
** =======================================================
*/

/* index of free-list header (after the predefined values) */
/// @brief 空闲表
#define freelist	(LUA_RIDX_LAST + 1)

/*
** The previously freed references form a linked list:
** t[freelist] is the index of a first free index, or zero if list is
** empty; t[t[freelist]] is the index of the second element; etc.
*/

/// @brief 针对栈顶的对象，创建并返回一个在索引 t 指向的表中的 引用（最后会弹出栈顶对象）。
// 此引用是一个唯一的整数键。只要你不向表 t 手工添加整数键，luaL_ref 可以保证它返回的键的唯一性。
// 你可以通过调用 lua_rawgeti(L, t, r) 来找回由r 引用的对象。函数 luaL_unref 用来释放一个引用关联的对象
// 如果栈顶的对象是 nil，luaL_ref 将返回常量LUA_REFNIL。常量 LUA_NOREF 可以保证和luaL_ref 能返回的其它引用值不同。
/// @param L 
/// @param t 
/// @return 
LUALIB_API int luaL_ref (lua_State *L, int t) {
  int ref;
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);  /* remove from stack */
    return LUA_REFNIL;  /* 'nil' has a unique fixed reference */
  }
  t = lua_absindex(L, t);
  if (lua_rawgeti(L, t, freelist) == LUA_TNIL) {  /* first access? */
    ref = 0;  /* list is empty */
    lua_pushinteger(L, 0);  /* initialize as an empty list */
    lua_rawseti(L, t, freelist);  /* ref = t[freelist] = 0 */
  }
  else {  /* already initialized */
    lua_assert(lua_isinteger(L, -1));
    ref = (int)lua_tointeger(L, -1);  /* ref = t[freelist] */
  }
  lua_pop(L, 1);  /* remove element from stack */
  if (ref != 0) {  /* any free element? */
    lua_rawgeti(L, t, ref);  /* remove it from list */
    lua_rawseti(L, t, freelist);  /* (t[freelist] = t[ref]) */
  }
  else  /* no free elements */
    ref = (int)lua_rawlen(L, t) + 1;  /* get a new reference */
  lua_rawseti(L, t, ref);
  return ref;
}

/// @brief 释放索引 t 处表的 ref 引用对象。此条目会从表中移除以让其引用的对象可被垃圾收集。而引用 ref 也被回收再次使用。
// 如果 ref 为 LUA_NOREF 或 LUA_REFNIL，luaL_unref 什么也不做。
/// @param L 
/// @param t 
/// @param ref 
/// @return 
LUALIB_API void luaL_unref (lua_State *L, int t, int ref) {
  if (ref >= 0) {
    t = lua_absindex(L, t);
    lua_rawgeti(L, t, freelist);
    lua_assert(lua_isinteger(L, -1));
    lua_rawseti(L, t, ref);  /* t[ref] = t[freelist] */
    lua_pushinteger(L, ref);
    lua_rawseti(L, t, freelist);  /* t[freelist] = ref */
  }
}

/* }====================================================== */


/*
** {======================================================
** Load functions
** =======================================================
*/

typedef struct LoadF {
  int n;  /* number of pre-read characters *///预读字符数
  FILE *f;  /* file being read *///正在读取的文件
  char buff[BUFSIZ];  /* area for reading file *///读到的文件内容块
} LoadF;

/// @brief 读取文件内容块
/// @param L 
/// @param ud 
/// @param size 
/// @return 
static const char *getF (lua_State *L, void *ud, size_t *size) {
  LoadF *lf = (LoadF *)ud;
  (void)L;  /* not used */
  if (lf->n > 0) {  /* are there pre-read characters to be read? */
    *size = lf->n;  /* return them (chars already in buffer) */
    lf->n = 0;  /* no more pre-read characters */
  }
  else {  /* read a block from file */
    /* 'fread' can return > 0 *and* set the EOF flag. If next call to
       'getF' called 'fread', it might still wait for user input.
       The next check avoids this problem. */
    if (feof(lf->f)) return NULL;
    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);  /* read block */
  }
  return lf->buff;
}

/// @brief 提示文件错误
/// @param L 
/// @param what 
/// @param fnameindex 
/// @return 
static int errfile (lua_State *L, const char *what, int fnameindex) {
  const char *serr = strerror(errno);
  const char *filename = lua_tostring(L, fnameindex) + 1;
  lua_pushfstring(L, "cannot %s %s: %s", what, filename, serr);
  lua_remove(L, fnameindex);
  return LUA_ERRFILE;
}

/// @brief 跳过utf-8 bom头
/// @param lf 
/// @return 
static int skipBOM (LoadF *lf) {
  const char *p = "\xEF\xBB\xBF";  /* UTF-8 BOM mark */
  int c;
  lf->n = 0;
  do {
    c = getc(lf->f);
    if (c == EOF || c != *(const unsigned char *)p++) return c;
    lf->buff[lf->n++] = c;  /* to be read by the parser */
  } while (*p != '\0');
  lf->n = 0;  /* prefix matched; discard it */
  return getc(lf->f);  /* return next character */
}


/*
** reads the first character of file 'f' and skips an optional BOM mark
** in its beginning plus its first line if it starts with '#'. Returns
** true if it skipped the first line.  In any case, '*cp' has the
** first "valid" character of the file (after the optional BOM and
** a first-line comment).
*/

/// @brief 跳过一些内容
// 读取文件“f”的第一个字符，并跳过其开头的可选 BOM 标记 
// 如果它以“#”开头，则跳过其第一行
// 如果跳过第一行，则返回 true
/// @param lf 
/// @param cp 
/// @return 
static int skipcomment (LoadF *lf, int *cp) {
  int c = *cp = skipBOM(lf);
  if (c == '#') {  /* first line is a comment (Unix exec. file)? */
    do {  /* skip first line */
      c = getc(lf->f);
    } while (c != EOF && c != '\n');
    *cp = getc(lf->f);  /* skip end-of-line, if present */
    return 1;  /* there was a comment */
  }
  else return 0;  /* no comment */
}

/// @brief 把一个文件加载为 Lua 代码块。这个函数使用 lua_load 加载文件中的数据。
// 代码块的名字被命名为 filename。如果 filename 为 NULL，它从标准输入加载。如果文件的第一行以 # 打头，则忽略这一行。
// mode 字符串的作用同函数 lua_load。
// 此函数的返回值和 lua_load 相同，不过它还可能产生一个叫做 LUA_ERRFILE的出错码。这种错误发生于无法打开或读入文件时，或是文件的模式错误。
// 和 lua_load 一样，这个函数仅加载代码块不运行
// /// @param L 
/// @param filename 
/// @param mode 
/// @return 
LUALIB_API int luaL_loadfilex (lua_State *L, const char *filename,
                                             const char *mode) {
  LoadF lf;
  int status, readstatus;
  int c;
  int fnameindex = lua_gettop(L) + 1;  /* index of filename on the stack *///读取文件名索引
  if (filename == NULL) {//文件是空的
    lua_pushliteral(L, "=stdin");
    lf.f = stdin;
  }
  else {
    lua_pushfstring(L, "@%s", filename);//把文件名压入堆栈顶
    lf.f = fopen(filename, "r");//只读方式打开
    if (lf.f == NULL) return errfile(L, "open", fnameindex);
  }
  if (skipcomment(&lf, &c))  /* read initial portion */
    lf.buff[lf.n++] = '\n';  /* add line to correct line numbers */
  if (c == LUA_SIGNATURE[0] && filename) {  /* binary file? */
    lf.f = freopen(filename, "rb", lf.f);  /* reopen in binary mode */
    if (lf.f == NULL) return errfile(L, "reopen", fnameindex);
    skipcomment(&lf, &c);  /* re-read initial portion */
  }
  if (c != EOF)
    lf.buff[lf.n++] = c;  /* 'c' is the first character of the stream */
  status = lua_load(L, getF, &lf, lua_tostring(L, -1), mode);
  readstatus = ferror(lf.f);
  if (filename) fclose(lf.f);  /* close file (even in case of errors) */
  if (readstatus) {
    lua_settop(L, fnameindex);  /* ignore results from 'lua_load' */
    return errfile(L, "read", fnameindex);
  }
  lua_remove(L, fnameindex);
  return status;
}

/// @brief 加载字符串
typedef struct LoadS {
  const char *s;
  size_t size;
} LoadS;

/// @brief 获取字符串
/// @param L 
/// @param ud 
/// @param size 
/// @return 
static const char *getS (lua_State *L, void *ud, size_t *size) {
  LoadS *ls = (LoadS *)ud;
  (void)L;  /* not used */
  if (ls->size == 0) return NULL;
  *size = ls->size;
  ls->size = 0;
  return ls->s;
}

/// @brief 把一段缓存加载为一个 Lua 代码块。这个函数使用 lua_load 来加载 buff 指向的长度为 sz 的内存区。
// 这个函数和 lua_load 返回值相同。name 作为代码块的名字，用于调试信息和错误消息。mode 字符串的作用同函数 lua_load
/// @param L 
/// @param buff 
/// @param size 
/// @param name 
/// @param mode 
/// @return 
LUALIB_API int luaL_loadbufferx (lua_State *L, const char *buff, size_t size,
                                 const char *name, const char *mode) {
  LoadS ls;
  ls.s = buff;
  ls.size = size;
  return lua_load(L, getS, &ls, name, mode);
}

/// @brief 将一个字符串加载为 Lua 代码块。这个函数使用 lua_load 加载一个零结尾的字符串s。
// 此函数的返回值和 lua_load 相同。
// 也和 lua_load 一样，这个函数仅加载代码块不运行。
/// @param L 
/// @param s 
/// @return 
LUALIB_API int luaL_loadstring (lua_State *L, const char *s) {
  return luaL_loadbuffer(L, s, strlen(s), s);
}

/* }====================================================== */


/// @brief 将索引 obj 处对象的元表中 e 域的值压栈。如果该对象没有元表，或是该元表没有相关域，此函数什么也不做,并返回 LUA_TNIL。
/// @param L 
/// @param obj 
/// @param event 
/// @return 
LUALIB_API int luaL_getmetafield (lua_State *L, int obj, const char *event) {
  if (!lua_getmetatable(L, obj))  /* no metatable? */
    return LUA_TNIL;
  else {
    int tt;
    lua_pushstring(L, event);
    tt = lua_rawget(L, -2);
    if (tt == LUA_TNIL)  /* is metafield nil? */
      lua_pop(L, 2);  /* remove metatable and metafield */
    else
      lua_remove(L, -2);  /* remove only metatable */
    return tt;  /* return metafield type */
  }
}

/// @brief 调用元方法
/// @param L 
/// @param obj 
/// @param event 
/// @return 
LUALIB_API int luaL_callmeta (lua_State *L, int obj, const char *event) {
  obj = lua_absindex(L, obj);
  if (luaL_getmetafield(L, obj, event) == LUA_TNIL)  /* no metafield? */
    return 0;
  lua_pushvalue(L, obj);
  lua_call(L, 1, 1);
  return 1;
}

/// @brief 以数字形式返回给定索引处值的“长度”；它等价于在 Lua 中调用 '#' 的操作
/// @param L 
/// @param idx 
/// @return 
LUALIB_API lua_Integer luaL_len (lua_State *L, int idx) {
  lua_Integer l;
  int isnum;
  lua_len(L, idx);
  l = lua_tointegerx(L, -1, &isnum);
  if (l_unlikely(!isnum))
    luaL_error(L, "object length is not an integer");
  lua_pop(L, 1);  /* remove object */
  return l;
}

/// @brief 将给定索引处的 Lua 值转换为一个相应格式的 C 字符串。
// 结果串不仅会压栈，还会由函数返回。如果 len 不为 NULL ，它还把字符串长度设到 *len 中。
// 如果该值有一个带 "__tostring" 域的元表，luaL_tolstring 会以该值为参数去调用对应的元方法，并将其返回值作为结果
/// @param L 
/// @param idx 
/// @param len 
/// @return 
LUALIB_API const char *luaL_tolstring (lua_State *L, int idx, size_t *len) {
  idx = lua_absindex(L,idx);
  if (luaL_callmeta(L, idx, "__tostring")) {  /* metafield? */
    if (!lua_isstring(L, -1))
      luaL_error(L, "'__tostring' must return a string");
  }
  else {
    switch (lua_type(L, idx)) {
      case LUA_TNUMBER: {
        if (lua_isinteger(L, idx))
          lua_pushfstring(L, "%I", (LUAI_UACINT)lua_tointeger(L, idx));
        else
          lua_pushfstring(L, "%f", (LUAI_UACNUMBER)lua_tonumber(L, idx));
        break;
      }
      case LUA_TSTRING:
        lua_pushvalue(L, idx);
        break;
      case LUA_TBOOLEAN:
        lua_pushstring(L, (lua_toboolean(L, idx) ? "true" : "false"));
        break;
      case LUA_TNIL:
        lua_pushliteral(L, "nil");
        break;
      default: {
        int tt = luaL_getmetafield(L, idx, "__name");  /* try name */
        const char *kind = (tt == LUA_TSTRING) ? lua_tostring(L, -1) :
                                                 luaL_typename(L, idx);
        lua_pushfstring(L, "%s: %p", kind, lua_topointer(L, idx));
        if (tt != LUA_TNIL)
          lua_remove(L, -2);  /* remove '__name' */
        break;
      }
    }
  }
  return lua_tolstring(L, -1, len);
}


/*
** set functions from list 'l' into table at top - 'nup'; each
** function gets the 'nup' elements at the top as upvalues.
** Returns with only the table at the stack.
*/

/// @brief 将所有luaL_Reg数组中的函数注册到通过luaL_newlib创建的table中.  
// 当upvalue个数不为0时,所创建的所有函数共享这些upvalue. -2到-(nup+1)的元素为要注册的upvalue. 
// (注意:这些upvalue是c中的upvalue,不是lua中的upvalue,可以在注册的c函数中通过 lua_upvalueindex(n)获取其值.) 
// 调用完成后弹出栈顶的所有upvalue.
/// @param L 
/// @param l luaL_Reg数组中函数
/// @param nup 上值数量
/// @return 
LUALIB_API void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup) {
  luaL_checkstack(L, nup, "too many upvalues"); //检测上值数量
  for (; l->name != NULL; l++) {  /* fill the table with given functions *///遍历luaL_Reg数组中的函数
    if (l->func == NULL)  /* place holder? *///到达了place luaL_Reg数组的{NULL, NULL}占位地方,也就是表示注册函数结束了
      lua_pushboolean(L, 0);//将布尔值压入堆栈。
    else {
      int i;
      for (i = 0; i < nup; i++)  /* copy upvalues to the top *///遍历上值
        lua_pushvalue(L, -nup);////将上值都拷贝到栈顶,注意这里传递的是nup,不是i
      lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues *///塞入一个闭包
    }
    lua_setfield(L, -(nup + 2), l->name);//将栈顶的回调函数和table关联起来,形如t[l->name] = 栈顶func 这里-(nup + 2)位置的table是通过luaL_newlib创建的
  }
  lua_pop(L, nup);  /* remove upvalues *///移除掉上值
}


/*
** ensure that stack[idx][fname] has a table and push that table
** into the stack
*/

/// @brief 将 t[fname] push到栈顶, 其中t是index处的table , 并且 t[fname] 也为一个table. 
// 如果 t[fname] 原本就存在,返回 true ,否则返回false,并且将 t[fname] 新建为一张空表. 
/// @param L 
/// @param idx 
/// @param fname 
/// @return 
LUALIB_API int luaL_getsubtable (lua_State *L, int idx, const char *fname) {
  if (lua_getfield(L, idx, fname) == LUA_TTABLE)
    return 1;  /* table already there */
  else {
    lua_pop(L, 1);  /* remove previous result */
    idx = lua_absindex(L, idx);
    lua_newtable(L);
    lua_pushvalue(L, -1);  /* copy to be left at top */
    lua_setfield(L, idx, fname);  /* assign new table to field */
    return 0;  /* false, because did not find table there */
  }
}


/*
** Stripped-down 'require': After checking "loaded" table, calls 'openf'
** to open a module, registers the result in 'package.loaded' table and,
** if 'glb' is true, also registers the result in the global table.
** Leaves resulting module on the top.
*/
LUALIB_API void luaL_requiref (lua_State *L, const char *modname,
                               lua_CFunction openf, int glb) {
  luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
  lua_getfield(L, -1, modname);  /* LOADED[modname] */
  if (!lua_toboolean(L, -1)) {  /* package not already loaded? */
    lua_pop(L, 1);  /* remove field */
    lua_pushcfunction(L, openf);
    lua_pushstring(L, modname);  /* argument to open function */
    lua_call(L, 1, 1);  /* call 'openf' to open module */
    lua_pushvalue(L, -1);  /* make copy of module (call result) */
    lua_setfield(L, -3, modname);  /* LOADED[modname] = module */
  }
  lua_remove(L, -2);  /* remove LOADED table */
  if (glb) {
    lua_pushvalue(L, -1);  /* copy of module */
    lua_setglobal(L, modname);  /* _G[modname] = module */
  }
}

/// @brief 将字符串 s 生成一个副本，并将其中的所有字符串p都替换为字符串r 。将结果串压栈并返回它。
/// @param b 
/// @param s 
/// @param p 
/// @param r 
/// @return 
LUALIB_API void luaL_addgsub (luaL_Buffer *b, const char *s,
                                     const char *p, const char *r) {
  const char *wild;
  size_t l = strlen(p);
  while ((wild = strstr(s, p)) != NULL) {
    luaL_addlstring(b, s, wild - s);  /* push prefix */
    luaL_addstring(b, r);  /* push replacement in place of pattern */
    s = wild + l;  /* continue after 'p' */
  }
  luaL_addstring(b, s);  /* push last suffix */
}

/// @brief 将字符串 s 生成一个副本，并将其中的所有字符串 p都替换为字符串 r 。将结果串压栈并返回它。
/// @param L 
/// @param s 
/// @param p 
/// @param r 
/// @return 
LUALIB_API const char *luaL_gsub (lua_State *L, const char *s,
                                  const char *p, const char *r) {
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  luaL_addgsub(&b, s, p, r);
  luaL_pushresult(&b);
  return lua_tostring(L, -1);
}

/// @brief 尝试重新调整之前调用 malloc 或 calloc 所分配的 ptr 所指向的内存块的大小。
/// @param ud 
/// @param ptr 指针指向一个要重新分配内存的内存块，该内存块之前是通过调用 malloc、calloc 或 realloc 进行分配内存的。如果为空指针，则会分配一个新的内存块，且函数返回一个指向它的指针。
/// @param osize 
/// @param nsize 内存块的新的大小，以字节为单位。如果大小为 0，且 ptr 指向一个已存在的内存块，则 ptr 所指向的内存块会被释放，并返回一个空指针。
/// @return 

static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;  /* not used */
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  else
    return realloc(ptr, nsize);
}

/// @brief 打印错误,并返回给lua进行程序中止
/// @param L 
/// @return 
static int panic (lua_State *L) {
  const char *msg = lua_tostring(L, -1);
  if (msg == NULL) msg = "error object is not a string";
  lua_writestringerror("PANIC: unprotected error in call to Lua API (%s)\n",
                        msg);
  return 0;  /* return to Lua to abort */
}


/*
** Warning functions:
** warnfoff: warning system is off
** warnfon: ready to start a new message
** warnfcont: previous message is to be continued
*/
static void warnfoff (void *ud, const char *message, int tocont);//警告系统已关闭
static void warnfon (void *ud, const char *message, int tocont);//警告准备启动新消息
static void warnfcont (void *ud, const char *message, int tocont);//警告上一条消息要继续


/*
** Check whether message is a control message. If so, execute the
** control or ignore it if unknown.
*/

/// @brief 检查消息是否为控制消息 如果是执行这个控制，否则不管他
/// @param L 
/// @param message 
/// @param tocont 
/// @return 
static int checkcontrol (lua_State *L, const char *message, int tocont) {
  if (tocont || *(message++) != '@')  /* not a control message? */
    return 0;
  else {
    if (strcmp(message, "off") == 0)
      lua_setwarnf(L, warnfoff, L);  /* turn warnings off */
    else if (strcmp(message, "on") == 0)
      lua_setwarnf(L, warnfon, L);   /* turn warnings on */
    return 1;  /* it was a control message */
  }
}


static void warnfoff (void *ud, const char *message, int tocont) {
  checkcontrol((lua_State *)ud, message, tocont);
}


/*
** Writes the message and handle 'tocont', finishing the message
** if needed and setting the next warn function.
*/
static void warnfcont (void *ud, const char *message, int tocont) {
  lua_State *L = (lua_State *)ud;
  lua_writestringerror("%s", message);  /* write message */
  if (tocont)  /* not the last part? */
    lua_setwarnf(L, warnfcont, L);  /* to be continued */
  else {  /* last part */
    lua_writestringerror("%s", "\n");  /* finish message with end-of-line */
    lua_setwarnf(L, warnfon, L);  /* next call is a new message */
  }
}


static void warnfon (void *ud, const char *message, int tocont) {
  if (checkcontrol((lua_State *)ud, message, tocont))  /* control message? */
    return;  /* nothing else to be done */
  lua_writestringerror("%s", "Lua warning: ");  /* start a new warning */
  warnfcont(ud, message, tocont);  /* finish processing */
}

/// @brief 创建一个新的 Lua 状态机。它以一个基于标准 C 的 realloc 函数实现的内存分配器调用 lua_newstate 。
// 并把可打印一些出错信息到标准错误输出的 panic 函数设置好，用于处理致命错误。
// 返回新的状态机。如果内存分配失败，则返回 NULL 。
/// @param  
/// @return 
LUALIB_API lua_State *luaL_newstate (void) {
  lua_State *L = lua_newstate(l_alloc, NULL);
  if (l_likely(L)) {
    lua_atpanic(L, &panic);
    lua_setwarnf(L, warnfoff, L);  /* default is warnings off */
  }
  return L;
}

/// @brief 检查参数中传入的版本与当前的lua版本号是否一致，如果版本号不一致，lua进程会异常退出 
/// @param L 
/// @param ver 
/// @param sz 
/// @return 
LUALIB_API void luaL_checkversion_ (lua_State *L, lua_Number ver, size_t sz) {
  lua_Number v = lua_version(L);//获取当前lua的版本号
  if (sz != LUAL_NUMSIZES)  /* check numeric types */
    luaL_error(L, "core and library have incompatible numeric types");
  else if (v != ver)
    luaL_error(L, "version mismatch: app. needs %f, Lua core provides %f",
                  (LUAI_UACNUMBER)ver, (LUAI_UACNUMBER)v);
}

