/*
 * @文件作用: 标准库中ctype相关实现
 * @功能分类: 内嵌库
 * @注释者: frog-game
 * @LastEditTime: 2023-01-22 11:45:17
*/

/*
** $Id: lctype.h $
** 'ctype' functions for Lua
** See Copyright Notice in lua.h
*/

#ifndef lctype_h
#define lctype_h

#include "lua.h"



/*
** WARNING: the functions defined here do not necessarily correspond
** to the similar functions in the standard C ctype.h. They are
** optimized for the specific needs of Lua.
*/

#if !defined(LUA_USE_CTYPE)

#if 'A' == 65 && '0' == 48
/* ASCII case: can use its own tables; faster and fixed */
#define LUA_USE_CTYPE	0
#else
/* must use standard C ctype */
#define LUA_USE_CTYPE	1
#endif

#endif


#if !LUA_USE_CTYPE	/* { */

#include <limits.h>

#include "llimits.h"


#define ALPHABIT	0//字母
#define DIGITBIT	1//数字
#define PRINTBIT	2//打印字符
#define SPACEBIT	3//空格
#define XDIGITBIT	4//16进制数字


#define MASK(B)		(1 << (B))


/*
** add 1 to char to allow index -1 (EOZ)
*/
#define testprop(c,p)	(luai_ctype_[(c)+1] & (p))

/*
** 'lalpha' (Lua alphabetic) and 'lalnum' (Lua alphanumeric) both include '_'
*/
#define lislalpha(c)	testprop(c, MASK(ALPHABIT))//判断是否是字母
#define lislalnum(c)	testprop(c, (MASK(ALPHABIT) | MASK(DIGITBIT)))//判断是否是字母或数字
#define lisdigit(c)	testprop(c, MASK(DIGITBIT))//判断是否是数字
#define lisspace(c)	testprop(c, MASK(SPACEBIT))//判断是否是空格
#define lisprint(c)	testprop(c, MASK(PRINTBIT))//判断是否可打印字符
#define lisxdigit(c)	testprop(c, MASK(XDIGITBIT))//是否为16进制数字


/*
** In ASCII, this 'ltolower' is correct for alphabetic characters and
** for '.'. That is enough for Lua needs. ('check_exp' ensures that
** the character either is an upper-case letter or is unchanged by
** the transformation, which holds for lower-case letters and '.'.)
*/

///将大写字母转换为小写字符
// 'A'的二进制表示是：01000001
// 'a'的二进制表示是：01100001
// 'A' ^ 'a'是100000
// 可以看出大写字母和小写字母是第5位不同,从0开始数
// 所以((c) | ('A' ^ 'a'))就可以大写字母变小写字母

#define ltolower(c)  \
  check_exp(('A' <= (c) && (c) <= 'Z') || (c) == ((c) | ('A' ^ 'a')),  \
            (c) | ('A' ^ 'a'))


/* one entry for each character and for -1 (EOZ) */ 
LUAI_DDEC(const lu_byte luai_ctype_[UCHAR_MAX + 2];)


#else			/* }{ */

/*
** use standard C ctypes
*/

#include <ctype.h>


#define lislalpha(c)	(isalpha(c) || (c) == '_')
#define lislalnum(c)	(isalnum(c) || (c) == '_')
#define lisdigit(c)	(isdigit(c))
#define lisspace(c)	(isspace(c))
#define lisprint(c)	(isprint(c))
#define lisxdigit(c)	(isxdigit(c))

#define ltolower(c)	(tolower(c))

#endif			/* } */

#endif

