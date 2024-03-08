/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>
#include <memory/vaddr.h>
/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>



static word_t eval(int p,int q, bool *sucess);
static word_t eval_token(int p,bool *sucess);
static word_t calbinary(word_t val1, word_t val2, int op, bool *sucess);
static word_t calunary(word_t val,int op,bool *sucess);

/* Start from 256, offset from ASCII character table
*/
enum {
  TK_NOTYPE = 256, 
  TK_EQ,TK_NEQ,TK_GT,TK_LT,TK_LE,TK_GE,
  TK_AND,TK_OR,
  TK_NUM,
  TK_HEX,
  TK_REG,
  TK_DEREF,
  TK_NEG
  /* TODO: Add more token types */

};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */

  {" +", TK_NOTYPE},    // spaces
  {"\\+", '+'},         // plus
  {"==", TK_EQ},        // equal
  {"!=",TK_NEQ},        // not equal
  {">",TK_GT},          // greater than
  {"<",TK_LT},          // less than
  {">=",TK_GE},         // greater or equal
  {"<=",TK_LE},         // less or equal
  {"&&",TK_AND},        // and
  {"\\|\\|",TK_OR},     // or
  {"-", '-'},           // minus
  {"\\*", '*'},         // multiply
  {"\\/", '/'},           // divide
  {"\\(", '('},         // left bracket
  {"\\)", ')'},         // right bracket
  {"0[xX][0-9a-fA-F]+",TK_HEX}, // hexical number
  {"[0-9]+", TK_NUM},   // decimal number
  {"\\$[a-zA-Z]+",TK_REG}, // register
  {"\\*",TK_DEREF},     // dereference
  {"\\-",TK_NEG},       // negative
};

#define NR_REGEX ARRLEN(rules)
#define OFTYPES(type, types) oftypes(type, types, ARRLEN(types))

static int bound_types[] = {')',TK_NUM,TK_HEX,TK_REG}; // boundary for binary operator
static int nop_types[] = {'(',')',TK_NUM,TK_HEX,TK_REG}; // not operator type
static int op1_types[] = {TK_NEG, TK_DEREF}; // unary operator type
static int logic_types[] = {TK_EQ,TK_NEQ,TK_GT,TK_LT,TK_LE,TK_GE,TK_AND,TK_OR}; // logic operator type
static bool oftypes(int type, int types[], int size) {
  for (int i = 0; i < size; i++) {
    if (type == types[i]) return true;
  }
  return false;
}

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

static Token tokens[32] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */
        if(rules[i].token_type == TK_NOTYPE) break;
        tokens[nr_token].type = rules[i].token_type;
        switch (rules[i].token_type) {
          case TK_NUM:
          case TK_HEX:
          case TK_REG:
            strncpy(tokens[nr_token].str,substr_start,substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            break;   
          case '*': case '-':
          if(nr_token==0||!OFTYPES(tokens[nr_token-1].type,bound_types)){
            switch (rules[i].token_type)
            {
            case '-':tokens[nr_token].type = TK_NEG;break;
            case '*':tokens[nr_token].type = TK_DEREF;break;
            }
          }
          break;
          default:
            if(OFTYPES(rules[i].token_type,logic_types)){
            strncpy(tokens[nr_token].str,substr_start,substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            break;
            }
        }
        nr_token++;
        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}
static bool check_parentgeses(int p,int q){
    int cnt = 0;
    for(int i=p;i<=q;i++){
      if(tokens[i].type=='(')cnt++;
      if(tokens[i].type==')')cnt--;
      if(cnt<0)return false;
      // right bracket is more than left bracket
      if(cnt==0&&i<q)return false;
      // (3+4)*(5+6) return false
    }
    if(cnt!=0)return false;
    return true;
}
// find the main operator 
static int priority(int p,int q){
  int ret = -1, bar = 0, op_type = 0;
  for(int i=p;i<=q;i++){
    if(tokens[i].type == '('){
      bar++;
      continue;
    }else if(tokens[i].type == ')'){
      if(bar==0){
        return -1;
      }
      bar--;
      continue;
    }else if(OFTYPES(tokens[i].type,nop_types)){
      continue;
    }else if(bar>0){
      continue;
    }else{
      int tmp_pre = 0;
      // deref,neg>logic>relationship>*/>+-
      switch (tokens[i].type)
      {
      case TK_DEREF:case TK_NEG:
        tmp_pre = 1;
        break;
      case TK_AND:case TK_OR:
        tmp_pre = 2;
        break;
      case TK_EQ:case TK_NEQ:case TK_GT:case TK_LT:case TK_LE:case TK_GE:
        tmp_pre = 3;
        break;
      case '*':case '/':
        tmp_pre = 4;
        break;
      case '+':case '-':
        tmp_pre = 5;
        break;
      default:
        return -1;
      }
      //1-1-1,前面的优先级高,**q后面的*优先级
      //判断是否为右结合的运算符
      if(tmp_pre>op_type||(tmp_pre==op_type&&!OFTYPES(tokens[i].type,op1_types))){
        op_type = tmp_pre;
        ret = i;
      }
    }
  }
  if(bar!=0)return -1;
  return ret;
}
static word_t eval(int p,int q, bool *sucess){
  *sucess = true;
  if(p>q){
    *sucess = false;
    return 0;
  }else if(p==q){
    return eval_token(p,sucess);
  }else if(check_parentgeses(p,q)==true){
    return eval(p+1,q-1,sucess);
  }else{
    int op = priority(p,q);
    if(op<0){
      *sucess = false;
      return 0;
    }
    bool flag1,flag2;
    word_t val1 = eval(p,op-1,&flag1);
    word_t val2 = eval(op+1,q,&flag2);
    if(flag2==false){
      *sucess = false;
      return 0;
    }
    if(flag1==true){
      word_t ret = calbinary(val1,val2,tokens[op].type,sucess);
      return ret;
    }else{
      word_t ret = calunary(val2,tokens[op].type,sucess);
      return ret;
    }
  }
}

word_t expr(char*e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }
  /* TODO: Insert codes to evaluate the expression. */
  return eval(0,nr_token-1,success);
  }

static word_t eval_token(int p,bool *sucess){
  switch (tokens[p].type)
  {
  case TK_NUM:
    return strtol(tokens[p].str,NULL,10);
    break;
  case TK_HEX:
    return strtol(tokens[p].str,NULL,16);
    break;
  case TK_REG:
    return isa_reg_str2val(tokens[p].str,sucess); 
  default:
    *sucess = false;
    return 0;
  }
}

// Binary Operation 
static word_t calbinary(word_t val1, word_t val2, int op, bool *sucess){
  switch (op)
  {
  case '+': return val1+val2;
  case '-': return val1-val2;
  case '*': return val1*val2;
  case '/':{  if(val2==0){
      *sucess = false;
      return 0;
    }
    return val1/val2;
  }
  case TK_EQ: return val1==val2;
  case TK_NEQ:return val1!=val2;
  case TK_GT: return val1>val2;
  case TK_LT: return val1<val2;
  case TK_GE: return val1>=val2;
  case TK_LE: return val1<=val2;
  case TK_AND:return val1&&val2;
  case TK_OR: return val1||val2;
  default:
    *sucess = false;
    return 0;
  }
}

static word_t calunary(word_t val,int op,bool *sucess){
  switch (op)
  {
  case TK_DEREF:
    return vaddr_read(val,MUXDEF(CONFIG_RV64,8,4));
  case TK_NEG:
    return -val;
  default:
    *sucess = false;
    return 0;
  }
}
