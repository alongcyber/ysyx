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
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <memory/vaddr.h>
#include "sdb.h"

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}


static int cmd_q(char *args) {
  nemu_state.state = NEMU_QUIT;
  return -1;
}

static int cmd_help(char *args);
static int cmd_s(char *args);
static int cmd_info(char *args);
static int cmd_x(char *args);
static int cmd_p(char *args);
static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },

  /* TODO: Add more commands */
  {"s","step inside",cmd_s},
  {"info","info r:print the value of all register \
info w: print the information of watchpoint\n",cmd_info},
  {"x","x N EXPR:Scan the memory",cmd_x},
  {"p","p EXPR:Print the value of the expression",cmd_p},
  
};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}
static int cmd_s(char*args){
  char *arg = strtok(NULL, " ");
  if(arg == NULL){
    /* no argument given*/
    cpu_exec(1);
  }
  else{
    int n = atoi(arg);
    if(n <= 0){
      printf("Invalid argument\n");
      return 0;
    }
    cpu_exec(n);
  }
  return 0;
}

static int cmd_info(char* args){
  char *arg = strtok(NULL, " ");
  if(arg == NULL){
    printf("Invalid argument\n");
  }
  else if(strcmp(arg,"r") == 0){
    isa_reg_display();
  }
  else if(strcmp(arg,"w") == 0){
    // display_wp();
    return 0;
  }
  else{printf("Unknown command '%s'\n",arg);}
  return 0;
}
// The vaddr_read(expr,4) depends on 4 (riscv32)
static int cmd_x(char *args){
  char *arg1 = strtok(args, " ");
  if(arg1 == NULL){
    printf("Invalid argument\n");
    printf("x N EXPR:Scan the memory\n");
    return 0;
  }
  char *arg2 = strtok(NULL, " ");
  if(arg2 == NULL){
    printf("Invalid argument\n");
    printf("x N EXPR:Scan the memory\n");
    return 0;
  }

  int n = strtol(arg1,NULL,10);
  vaddr_t expr = strtol(arg2,NULL,16);
    int i, j;
  for (i = 0; i < n;) {
    // Makes it easier for the program 
    //to run on machines with different word widths.
    int len = MUXDEF(CONFIG_RV64, 8, 4);
    int width = MUXDEF(CONFIG_RV64, 18, 10);
    printf(ANSI_FMT("%#0*x: ", ANSI_FG_CYAN),width,expr);
    for (j = 0; i < n && j < len; i++, j++) {
      word_t w = vaddr_read(expr, len);
      expr += len;
      printf("%#0*x ",width,w);
    }
    puts(""); 
  }
  return 0;
}

static int cmd_p(char *args){
  char *arg = strtok(NULL, " ");
  if(arg == NULL){
    printf("Invalid argument\n");
    printf("p EXPR:Print the value of the expression\n");
  }
  else{
    bool success = true;
    sword_t result = expr(arg,&success);
    if(success){
      printf("%d\n",result);
    }
    else{
      printf("Invalid expression\n");
    }
  }
  return 0;
}
void sdb_set_batch_mode() {
  is_batch_mode = true;
}

void sdb_mainloop() {
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  for (char *str; (str = rl_gets()) != NULL; ) {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
