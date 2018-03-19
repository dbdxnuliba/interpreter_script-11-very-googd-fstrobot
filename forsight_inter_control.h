#ifndef FORSIGHT_INTER_CONTROL_H
#define FORSIGHT_INTER_CONTROL_H

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#ifdef WIN32
#include <windows.h>
#include <process.h>
#pragma warning(disable : 4786)
#endif
#include <vector>
#include <string>
#include <string.h>
#include <fstream>

#ifndef WIN32
#include <unistd.h>
#endif

#include "forsight_interpreter_shm.h"
#include "interpreter_common.h"
#include "forsight_basint.h"

// #define USE_WAITING_R

bool parseScript(const char* fname);
// void findLoopEnd(int index);
InterpreterState getPrgmState();
void setPrgmState(InterpreterState state);
void setCurLine(int line);
#ifdef WIN32
void setWarning(__int64 warn);
#else
void setWarning(long long int warn);
#endif 
void getSendPermission();
bool setInstruction(struct thread_control_block * objThdCtrlBlockPtr, Instruction * instruction);
bool getIntprtCtrl();
void executeBlock();
void executeLoop(int loop_cnt);
void executeScript();
#ifdef WIN32
unsigned __stdcall script_func(void* arg);
#else
void* script_func(void* arg);
#endif
void parseCtrlComand(); // (struct thread_control_block * objThdCtrlBlock);
void initShm();
vector<string> split(string str,string pattern);

void waitInterpreterStateleftWaiting(
	struct thread_control_block * objThdCtrlBlockPtr);
void waitInterpreterStateleftPaused(
	struct thread_control_block * objThdCtrlBlockPtr);

void waitInterpreterStateToPaused(
	struct thread_control_block * objThdCtrlBlockPtr);

#endif







