/*
 * @文件作用: 词法分析器。由lparser.c使用
 * @功能分类: 源代码解析以及预编译字节码
 * @注释者: frog-game
 * @LastEditTime: 2023-01-29 20:10:35
 */

/*
** $Id: llex.h $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include <limits.h>

#include "lobject.h"
#include "lzio.h"


/*
** Single-char tokens (terminal symbols) are represented by their own
** numeric code. Other tokens start at the following value.
*/
#define FIRST_RESERVED	(UCHAR_MAX + 1)


#if !defined(LUA_ENV)
#define LUA_ENV		"_ENV"
#endif


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
*/

/// @brief 预留关键字
enum RESERVED {
  /* terminal symbols denoted by reserved words *///系统默认关键字
  TK_AND = FIRST_RESERVED, TK_BREAK,
  TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION,
  TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
  TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
  /* other terminal symbols *///其它关键字 
  TK_IDIV, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE,
  TK_SHL, TK_SHR,
  TK_DBCOLON, TK_EOS,
  TK_FLT, TK_INT, TK_NAME, TK_STRING
};

/* number of reserved words */
///预留单词数量
#define NUM_RESERVED	(cast_int(TK_WHILE-FIRST_RESERVED + 1))


/// @brief 语义辅助信息
typedef union {
  lua_Number r;//数值类型
  lua_Integer i;//整型
  TString *ts;//字符串
} SemInfo;  /* semantics information */

/// @brief 语义分割最小单位Token，用来表示程序中最基本的一个元素。举例来说，编程语言中的每个关键字、操作符、Identifier、数字、字符串等，都是 Token
typedef struct Token {
  int token;//Token实例的类型
  SemInfo seminfo;//用来保存token对应类型的实际值
} Token;


/* state of the lexer plus state of the parser when shared by all
   functions */

/// @brief 词法分析的context数据
// 不仅用于保存当前的词法分析状态信息，而且也保存了整个编译系统的全局状态
typedef struct LexState {
  int current;  /* current character (charint) *///当前读入的字符
  int linenumber;  /* input line counter *///输入的行的计数器
  int lastline;  /* line of last token 'consumed' *///上一个行
  Token t;  /* current token *///当前对比token
  Token lookahead;  /* look ahead token *///提前获取的token
  struct FuncState *fs;  /* current function (parser) *///当前解析的方法
  struct lua_State *L;//lua_State虚拟机全局状态机，这里不做深入介绍
  ZIO *z;  /* input stream *///这里可以通过lzio.h看出，这里是字符流，通过这里读取输入的字符流
  Mbuffer *buff;  /* buffer for tokens *///tokens的流存储器
  Table *h;  /* to avoid collection/reuse strings *///常量缓存表，用于缓存lua代码中的常量，加快编译时的常量查找
  struct Dyndata *dyd;  /* dynamic structures used by the parser *///语法分析过程中，存放local变量信息的结构
  TString *source;  /* current source name *///这里存储的是当前资源的名字
  TString *envn;  /* environment variable name *///环境变量名称
} LexState;


LUAI_FUNC void luaX_init (lua_State *L);
LUAI_FUNC void luaX_setinput (lua_State *L, LexState *ls, ZIO *z,
                              TString *source, int firstchar);
LUAI_FUNC TString *luaX_newstring (LexState *ls, const char *str, size_t l);
LUAI_FUNC void luaX_next (LexState *ls);
LUAI_FUNC int luaX_lookahead (LexState *ls);
LUAI_FUNC l_noret luaX_syntaxerror (LexState *ls, const char *s);
LUAI_FUNC const char *luaX_token2str (LexState *ls, int token);


#endif
