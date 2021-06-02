#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emulator.h"
#include "math.h"

#define XSTR(x) STR(x)		//can be used for MAX_ARG_LEN in sscanf
#define STR(x) #x

#define ADDR_TEXT    0x00400000 //where the .text area starts in which the program lives
#define TEXT_POS(a)  ((a==ADDR_TEXT)?(0):(a - ADDR_TEXT)/4) //can be used to access text[]


const char *register_str[] = {"$zero",
                              "$at", "$v0", "$v1",
                              "$a0", "$a1", "$a2", "$a3",
                              "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
                              "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
                              "$t8", "$t9",
                              "$k0", "$k1",
                              "$gp",
                              "$sp", "$fp", "$ra"};

/* Space for the assembler program */
char prog[MAX_PROG_LEN][MAX_LINE_LEN];
int prog_len = 0;

/* Elements for running the emulator */
unsigned int registers[MAX_REGISTER] = {0}; // the registers
unsigned int pc = 0;                        // the program counter
unsigned int text[MAX_PROG_LEN] = {0}; // the text memory with our instructions

/* function to create bytecode for instruction nop
   conversion result is passed in bytecode
   function always returns 0 (conversion OK) */
typedef int (*opcode_function)(unsigned int, unsigned int*, char*, char*, char*, char*);

int add_imi(unsigned int *bytecode, int imi){
	if (imi<-32768 || imi>32767) return (-1);
	*bytecode|= (0xFFFF & imi);
	return(0);
}

int add_sht(unsigned int *bytecode, int sht){
	if (sht<0 || sht>31) return(-1);
	*bytecode|= (0x1F & sht) << 6;
	return(0);
}

int add_reg(unsigned int *bytecode, char *reg, int pos){
	int i;
	for(i=0;i<MAX_REGISTER;i++){
		if(!strcmp(reg,register_str[i])){
		*bytecode |= (i << pos);
			return(0);
		}
	}
	return(-1);
}

int add_lbl(unsigned int offset, unsigned int *bytecode, char *label){
	char l[MAX_ARG_LEN+1];
	int j=0;
	while(j<prog_len){
		memset(l,0,MAX_ARG_LEN+1);
		sscanf(&prog[j][0],"%" XSTR(MAX_ARG_LEN) "[^:]:", l);
		if (label!=NULL && !strcmp(l, label)) return(add_imi( bytecode, j-(offset+1)) );
		j++;
	}
	return (-1);
}

int opcode_nop(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0;
	return (0);
}

int opcode_add(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x20; 				// op,shamt,funct
	if (add_reg(bytecode,arg1,11)<0) return (-1); 	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_reg(bytecode,arg3,16)<0) return (-1);	// source2 register
	return (0);
}

int opcode_addi(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x20000000; 				// op
	if (add_reg(bytecode,arg1,16)<0) return (-1);	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_imi(bytecode,atoi(arg3))) return (-1);	// constant
	return (0);
}

int opcode_andi(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x30000000; 				// op
	if (add_reg(bytecode,arg1,16)<0) return (-1); 	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_imi(bytecode,atoi(arg3))) return (-1);	// constant
	return (0);
}

int opcode_beq(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x10000000; 				// op
	if (add_reg(bytecode,arg1,21)<0) return (-1);	// register1
	if (add_reg(bytecode,arg2,16)<0) return (-1);	// register2
	if (add_lbl(offset,bytecode,arg3)) return (-1); // jump
	return (0);
}

int opcode_bne(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x14000000; 				// op
	if (add_reg(bytecode,arg1,21)<0) return (-1); 	// register1
	if (add_reg(bytecode,arg2,16)<0) return (-1);	// register2
	if (add_lbl(offset,bytecode,arg3)) return (-1); // jump
	return (0);
}

int opcode_srl(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x2; 					// op
	if (add_reg(bytecode,arg1,11)<0) return (-1);   // destination register
	if (add_reg(bytecode,arg2,16)<0) return (-1);   // source1 register
	if (add_sht(bytecode,atoi(arg3))<0) return (-1);// shift
	return(0);
}

int opcode_sll(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0; 					// op
	if (add_reg(bytecode,arg1,11)<0) return (-1);	// destination register
	if (add_reg(bytecode,arg2,16)<0) return (-1); 	// source1 register
	if (add_sht(bytecode,atoi(arg3))<0) return (-1);// shift
	return(0);
}

const char *opcode_str[] = {"nop", "add", "addi", "andi", "beq", "bne", "srl", "sll"};
opcode_function opcode_func[] = {&opcode_nop, &opcode_add, &opcode_addi, &opcode_andi, &opcode_beq, &opcode_bne, &opcode_srl, &opcode_sll};

/* a function to print the state of the machine */
int print_registers() {
  int i;
  printf("registers:\n");
  for (i = 0; i < MAX_REGISTER; i++) {
    printf(" %d: %d\n", i, registers[i]);
  }
  printf(" Program Counter: 0x%08x\n", pc);
  return (0);
}

//a method to transform the value
int btod(char *bin, int bits){
  int i = 0;
  int value = 0;
    while(bin[i] != '\0'){
      if(bin[i] == '1'){ 
        value = value + pow(2,(bits-1)-i); // add 2^(bits-1)-i to the value, e.g if bits = 5, then it'll do 2^5, 2^4, 2^3.....2^0
      }
      i++;
    }
    return value;
}

//transforms the above taken hex values into binary 
void hexToBinFunc(char *byte, char *binary){
  int i = 2;
  while(byte[i]){
    switch(byte[i]){
      //concatenates the correspodning binary value to each hexadecimal value
      case '0':
        strcat(binary, "0000");
        break;
      case '1':
        strcat(binary, "0001");
        break;
      case '2':
        strcat(binary, "0010");
        break;
      case '3':
        strcat(binary, "0011");
        break;
      case '4':
        strcat(binary, "0100");
        break;
      case '5':
        strcat(binary, "0101");
        break;
      case '6':
        strcat(binary, "0110");
        break;
      case '7':
        strcat(binary, "0111");
        break;
      case '8':
        strcat(binary, "1000");
        break;
      case '9':
        strcat(binary, "1001");
        break;
      case 'a':
        strcat(binary, "1010");
        break;
      case 'b':
        strcat(binary, "1011");
        break;
      case 'c':
        strcat(binary, "1100");
        break;
      case 'd':
        strcat(binary, "1101");
        break;
      case 'e':
        strcat(binary, "1110");
        break;
      case 'f':
        strcat(binary, "1111");
        break;
      
    }
    i++;
  }
}

/* function to execute bytecode */
int exec_bytecode() {
  printf("EXECUTING PROGRAM ...\n");
  pc = ADDR_TEXT; // set program counter to the start of our program

  // here goes the code to run the byte code

  while(text[TEXT_POS(pc)]){
    char bin[33] = ""; //bytecode as binary 
    char byte[11] = ""; //bytecode as string

    snprintf(byte, 11, "0x%08x", text[TEXT_POS(pc)]); //converts bytecode number to string format
    hexToBinFunc(byte, bin); //converts bytecode to binary string

    //checking the difference between r-type or i-type instruction
    char op[7];
    memcpy(op, &bin, 6);
    op[6] = '\0';

    if(!strcmp(op, "000000")){ //all r-type instrunctions have an opcode of 000000
      //do r-type instrunctions here
      char rs[6], rt[6], rd[6], shamt[6], funct[7];
      //these memcpy functions grab their required substring from the binary string 
      memcpy(rs, &bin[6],5); // takes a substring of bin and stores it in rs
      rs[5] = '\0';
      memcpy(rt, &bin[11],5);
      rt[5] = '\0';
      memcpy(rd, &bin[16],5);
      rd[5] = '\0';
      memcpy(shamt, &bin[21],5);
      shamt[5] = '\0';
      memcpy(funct, &bin[26],6);
      funct[6] = '\0';
      

      int rs_int = btod(rs, 5);
      int rt_int = btod(rt, 5);
      int rd_int = btod(rd, 5);
      int shamt_int = btod(shamt, 5);

      //use funct to decipher correct r-type function
      if(!strcmp(funct, "100000")){ //this is the funct code for add
        registers[rd_int] = registers[rs_int] + registers[rt_int]; 
      }
      else if (!strcmp(funct, "000000")){
        registers[rd_int] = registers[rd_int] * pow(2, shamt_int); //shifts left by shamt_int
      }
      else if (!strcmp(funct, "000010")){
        registers[rd_int] = registers[rd_int] * pow(0.5, shamt_int); //shifts right by sham_int
      }

    } else {

      //do i-type instructions here
      char rs[6], rt[6];

      memcpy(rs, &bin[11],5); 
      rs[5] = '\0'; //rs register
      memcpy(rt, &bin[6],5);
      rt[5] = '\0'; //rt register

      int rs_int = btod(rs, 5);
      int rt_int = btod(rt, 5);

      char immediate_value[17];
      memcpy(immediate_value, &bin[16], 16);

      immediate_value[16] = '\0';

      //convert immediate binary to int 
      int i = 0;
      int immediate = btod(immediate_value, 16);

      

      if(!strcmp(op, "001000")){ //if opcode equals to the addi opcode
        if(immediate > 32768){ //conversion of 2's complement
          immediate = immediate - 65536;
        }
        registers[rs_int] = registers[rt_int] + immediate;
      }

      else if(!strcmp(op, "001100")){ //if opcode equals to the andi opcode
        registers[rs_int] = registers[rt_int] & immediate;
      }

    
      else if(!strcmp(op, "000100")){ //if opcode equals to the beq opcode
        if(immediate > 32768){
          immediate = immediate - 65536;
        }
        if(registers[rs_int] == registers[rt_int]){
          pc = pc + immediate*4;
        }
      }


      else if(!strcmp(op, "000101")){ //if opcode equals to the bne opcode
        if(immediate > 32768){
          immediate = immediate - 65536;
        }
        if(!(registers[rs_int] == registers[rt_int])){
          pc = pc + immediate*4;
        }
      }
    }

    pc = pc + 4;
  }

  print_registers(); // print out the state of registers at the end of execution

  printf("... DONE!\n");
  return (0);
}

/* function to create bytecode */
int make_bytecode() {
  unsigned int
      bytecode; // holds the bytecode for each converted program instruction
  int i, j = 0;    // instruction counter (equivalent to program line)

  char label[MAX_ARG_LEN + 1];
  char opcode[MAX_ARG_LEN + 1];
  char arg1[MAX_ARG_LEN + 1];
  char arg2[MAX_ARG_LEN + 1];
  char arg3[MAX_ARG_LEN + 1];

  printf("ASSEMBLING PROGRAM ...\n");
  while (j < prog_len) {
    memset(label, 0, sizeof(label));
    memset(opcode, 0, sizeof(opcode));
    memset(arg1, 0, sizeof(arg1));
    memset(arg2, 0, sizeof(arg2));
    memset(arg3, 0, sizeof(arg3));

    bytecode = 0;

    if (strchr(&prog[j][0], ':')) { // check if the line contains a label
      if (sscanf(
              &prog[j][0],
              "%" XSTR(MAX_ARG_LEN) "[^:]: %" XSTR(MAX_ARG_LEN) "s %" XSTR(
                  MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s",
              label, opcode, arg1, arg2,
              arg3) < 2) { // parse the line with label
        printf("parse error line %d\n", j);
        return (-1);
      }
    } else {
      if (sscanf(&prog[j][0],
                 "%" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s %" XSTR(
                     MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s",
                 opcode, arg1, arg2,
                 arg3) < 1) { // parse the line without label
        printf("parse error line %d\n", j);
        return (-1);
      }
    }

    for (i=0; i<MAX_OPCODE; i++){
        if (!strcmp(opcode, opcode_str[i]) && ((*opcode_func[i]) != NULL))
        {
            if ((*opcode_func[i])(j, &bytecode, opcode, arg1, arg2, arg3) < 0)
            {
                printf("ERROR: line %d opcode error (assembly: %s %s %s %s)\n", j, opcode, arg1, arg2, arg3);
                return (-1);
            }
            else
            {
                printf("0x%08x 0x%08x\n", ADDR_TEXT + 4 * j, bytecode);
                text[j] = bytecode;
                break;
            }
        }
        if (i == (MAX_OPCODE - 1))
        {
            printf("ERROR: line %d unknown opcode\n", j);
            return (-1);
        }
    }

    j++;
  }
  printf("... DONE!\n");
  return (0);
}

/* loading the program into memory */
int load_program() {
  int j = 0;
  FILE *f;

  printf("LOADING PROGRAM ...\n");

  f = fopen("prog.txt", "r");
  while (fgets(&prog[prog_len][0], MAX_LINE_LEN, f) != NULL) {
    prog_len++;
  }

  printf("PROGRAM:\n");
  for (j = 0; j < prog_len; j++) {
    printf("%d: %s", j, &prog[j][0]);
  }

  return (0);
}
