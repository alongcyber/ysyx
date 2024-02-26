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
            strncpy(tokens[nr_token].str,substr_start,substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            break;
          case TK_HEX:
            strncpy(tokens[nr_token].str,substr_start,substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            break;
          case TK_REG:
            strncpy(tokens[nr_token].str,substr_start,substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            break;
        }
        nr_token++;
        break;
      }
    }
    for (int i = 0; i < nr_token; i++)
    {
      if(tokens[i].type=='*' && (i==0 || ((tokens[i-1].type!=TK_NUM) || (tokens[i-1].type=='(')))){
        tokens[i].type = TK_DEREF;
      }
      if(tokens[i].type=='-' && (i==0 || ((tokens[i-1].type!=TK_NUM) || (tokens[i-1].type=='(')))){
        tokens[i].type = TK_NEG;
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
static int prioty(int p,int q){
  int ret = -1, bar = 0, op_type = 0;
  for (int i = p; i <= q; i++) {
    if (tokens[i].type == TK_NUM) {
      continue;
    }
    if (tokens[i].type == '(') {
      bar++;
    } else if (tokens[i].type == ')') {
      if (bar == 0) {
        return -1;
      }
      bar--;
    } else if (bar > 0) {
      continue;
    } else {
      int tmp_type = 0;
      switch (tokens[i].type) {
      case TK_NEG: case TK_DEREF: {
        tmp_type = 1; 
        break;
      }
      case '*': case '/': tmp_type = 2; break;
      case '+': case '-': tmp_type = 3; break;
      default: assert(0);
      }
      if ((tmp_type >= op_type)&&(tmp_type!=1)) {
        op_type = tmp_type;
        ret = i;
      }else if((tmp_type>op_type)&&(tmp_type==1)){
        op_type = tmp_type;
        ret = i;
      }
    }
  }
  if (bar != 0) return -1;
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
    int op = prioty(p,q);
    if(op<0){
      *sucess = false;
      return 0;
    }
    bool flag1,flag2;
    word_t  val1 = eval(p,op-1,&flag1);
    word_t val2 = eval(op+1,q,&flag2);
    if(flag2==false){
      *sucess = false;
      return 0;
    }
    if(flag1==true){
      word_t ret = calbinary(val1,val2,tokens[op].type,sucess);
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
    return vaddr_read(val,4);
  case TK_NEG:
    return -val;
  default:
    *sucess = false;
    return 0;
  }
}