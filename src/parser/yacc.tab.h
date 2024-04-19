/* A Bison parser, made by GNU Bison 3.5.1.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2020 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* Undocumented macros, especially those whose name start with YY_,
   are private implementation details.  Do not rely on them.  */

#ifndef YY_YY_HOME_HARVEY_DOCUMENTS_RMDB_SRC_PARSER_YACC_TAB_H_INCLUDED
# define YY_YY_HOME_HARVEY_DOCUMENTS_RMDB_SRC_PARSER_YACC_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    SHOW = 258,
    TABLES = 259,
    CREATE = 260,
    TABLE = 261,
    DROP = 262,
    LOAD = 263,
    DESC = 264,
    INSERT = 265,
    INTO = 266,
    VALUES = 267,
    DELETE = 268,
    FROM = 269,
    ASC = 270,
    ORDER = 271,
    BY = 272,
    WHERE = 273,
    UPDATE = 274,
    SET = 275,
    SELECT = 276,
    INT = 277,
    BIGINT = 278,
    CHAR = 279,
    FLOAT = 280,
    DATETIME = 281,
    INDEX = 282,
    AND = 283,
    JOIN = 284,
    EXIT = 285,
    HELP = 286,
    TXN_BEGIN = 287,
    TXN_COMMIT = 288,
    TXN_ABORT = 289,
    TXN_ROLLBACK = 290,
    ORDER_BY = 291,
    LIMIT = 292,
    AS = 293,
    SUM = 294,
    COUNT = 295,
    MAX = 296,
    MIN = 297,
    OUTPUT_FILE = 298,
    OFF = 299,
    LEQ = 300,
    NEQ = 301,
    GEQ = 302,
    T_EOF = 303,
    IDENTIFIER = 304,
    VALUE_STRING = 305,
    VALUE_INT = 306,
    VALUE_FLOAT = 307,
    VALUE_BIGINT = 308,
    VALUE_DATETIME = 309,
    VALUE_FILEPATH = 310
  };
#endif

/* Value type.  */

/* Location type.  */
#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE YYLTYPE;
struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif



int yyparse (void);

#endif /* !YY_YY_HOME_HARVEY_DOCUMENTS_RMDB_SRC_PARSER_YACC_TAB_H_INCLUDED  */
