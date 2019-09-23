#ifdef WIN32
#pragma warning(disable : 4786)
#endif
#include<libxml/parser.h>

#include "forsight_inter_control.h"
#include "forsight_innercmd.h"
#include "forsight_io_controller.h"
#ifndef WIN32
// #include "io_interface.h"
#include "error_code.h"
#endif

#ifdef USE_FORSIGHT_REGISTERS_MANAGER
#ifndef WIN32
#include "reg_manager/reg_manager_interface_wrapper.h"
using namespace fst_ctrl ;
#else
#include "forsight_io_mapping.h"
#include "forsight_launch_code_startup.h"
#include "forsight_macro_instr_startup.h"
#endif
#include "reg_manager/forsight_registers_manager.h"
#else
#include "reg-shmi/forsight_registers.h"
#include "reg-shmi/forsight_op_regs_shmi.h"
#endif
#include "launch_code_mgr.h"

#define VELOCITY    (500)
using namespace std;

#define MAX_WAIT_SECOND     30

// bool is_backward= false;

// extern jmp_buf e_buf; /* hold environment for longjmp() */
extern struct thread_control_block g_thread_control_block[NUM_THREAD + 1];

#ifdef WIN32
extern HANDLE    g_basic_interpreter_handle[NUM_THREAD + 1];
#else
extern pthread_t g_basic_interpreter_handle[NUM_THREAD + 1];
#endif
int  g_iCurrentThreadSeq = -1 ;  // minus one add one equals to zero

std::string g_files_manager_data_path = "";
int         g_wait_time_out_config    = -1;

vector<key_variable> g_vecKeyVariables ;

static InterpreterState g_privateInterpreterState;
InterpreterPublish  g_interpreter_publish; 

LaunchCodeMgr *    g_launch_code_mgr_ptr; 
HomePoseMgr *      g_home_pose_mgr_ptr; 

/************************************************* 
	Function:		split
	Description:	split string to vector
	Input:			str               - soruce string
	Input:			pattern           - seperator
	Return: 		vector<string>
*************************************************/ 
vector<string> split(string str,string pattern)
{
	string::size_type pos;
	vector<string> result;
	str+=pattern;
	int size=str.size();

	for(int i=0; i<size; i++)
	{
		pos=str.find(pattern,i);
		if((int)pos<size)
		{
			string s=str.substr(i,pos-i);
			result.push_back(s);
			i=pos+pattern.size()-1;
		}
	}
	return result;
}

/************************************************* 
	Function:		getMoveCommandDestination
	Description:	get start position of MOV* (Used in the BACKWARD)
	Input:			movCmdDst        - start position of MOV*
	Return: 		NULL
*************************************************/ 
void getMoveCommandDestination(MoveCommandDestination& movCmdDst)
{
#ifndef WIN32
	  movCmdDst.posture = {1, 1, -1, 0};
#endif 
	  forgesight_registers_manager_get_joint(movCmdDst.joint_target);
	  forgesight_registers_manager_get_cart(movCmdDst.pose_target);
	  forgesight_registers_manager_get_posture(movCmdDst.posture);
	  forgesight_registers_manager_get_turn(movCmdDst.turn);
#ifndef WIN32
	  FST_INFO("getMoveCommandDestination: Forward movej to TYPE_PR:(%f, %f, %f, %f, %f, %f) ", 
		  movCmdDst.joint_target.j1_, movCmdDst.joint_target.j2_, 
		  movCmdDst.joint_target.j3_, movCmdDst.joint_target.j4_, 
		  movCmdDst.joint_target.j5_, movCmdDst.joint_target.j6_);
	  FST_INFO("getMoveCommandDestination: Forward movel to POSE:(%f, %f, %f, %f, %f, %f) in MovL", 
		  movCmdDst.pose_target.point_.x_, movCmdDst.pose_target.point_.y_, 
		  movCmdDst.pose_target.point_.z_, movCmdDst.pose_target.euler_.a_, 
			movCmdDst.pose_target.euler_.b_, movCmdDst.pose_target.euler_.c_);
#endif 
//    readShm(SHM_INTPRT_DST, 0, (void*)&movCmdDst, sizeof(movCmdDst));
}

/************************************************* 
	Function:		resetProgramNameAndLineNum
	Description:	clear Line number and ProgramName
	Input:			thread_control_block   - interpreter info
	Return: 		NULL
*************************************************/ 
void resetProgramNameAndLineNum(struct thread_control_block * objThdCtrlBlockPtr)
{
	setCurLine(objThdCtrlBlockPtr, (char *)"", 0);
	setProgramName(objThdCtrlBlockPtr, (char *)""); 
}

#ifndef WIN32
InterpreterPublish* getInterpreterPublishPtr()
{
	return &g_interpreter_publish ;
}
#endif
/************************************************* 
	Function:		getProgramName
	Description:	get running Program Name
	Return: 		Running Program Name
*************************************************/ 
char * getProgramName()
{
	return g_interpreter_publish.program_name; 
}

/************************************************* 
	Function:		setProgramName
	Description:	set running Program Name
	Input:			thread_control_block   - interpreter info
	Input:			program_name           - Running Program Name
	Return: 		NULL
*************************************************/ 
void setProgramName(struct thread_control_block * objThdCtrlBlockPtr, char * program_name)
{
    FST_INFO("setProgramName to %s at %d", program_name, objThdCtrlBlockPtr->is_main_thread);
	if(objThdCtrlBlockPtr->is_main_thread == MAIN_THREAD)
	{
		strcpy(g_interpreter_publish.program_name, program_name); 
	}
	else
	{
		FST_INFO("setProgramName Failed to %s", program_name);
	}
}

/************************************************* 
	Function:		getThreadControlBlock
	Description:	Find a free thread_control_block object in the g_thread_control_block
	Input:			NULL
	Return: 		a free thread_control_block object 
*************************************************/ 
struct thread_control_block *  getThreadControlBlock(bool isUploadError)
{
	if(getCurrentThreadSeq() < 0)
    {
        FST_ERROR("getThreadControlBlock failed from %d", getCurrentThreadSeq());
		if(isUploadError)
			setWarning(INFO_INTERPRETER_THREAD_NOT_EXIST);
		return NULL;
	}
	else
    {
    	FST_INFO("getThreadControlBlock at %d", getCurrentThreadSeq());
		return &g_thread_control_block[getCurrentThreadSeq()] ;
	}
}

/************************************************* 
	Function:		getCurrentThreadSeq
	Description:	get current running thread_control_block index
	Input:			NULL
	Return: 		a free thread_control_block object index
*************************************************/ 
int getCurrentThreadSeq(bool isUploadError)
{
	if(g_iCurrentThreadSeq < 0)
	{
		if(isUploadError)
			setWarning(INFO_INTERPRETER_THREAD_NOT_EXIST);
	}
	return g_iCurrentThreadSeq ;
}

/************************************************* 
	Function:		incCurrentThreadSeq
	Description:	increment current running thread_control_block index
	Input:			NULL
	Return: 		NULL
*************************************************/ 
void incCurrentThreadSeq()
{
	if(g_iCurrentThreadSeq < NUM_THREAD)
		g_iCurrentThreadSeq++ ;
	else
		g_iCurrentThreadSeq = 0 ;
}

/************************************************* 
	Function:		getPrgmState
	Description:	get current Program State
	Input:			NULL
	Return: 		current Program State
*************************************************/ 
InterpreterState getPrgmState()
{
	return g_privateInterpreterState ;
}

/************************************************* 
	Function:		setPrgmState
	Description:	set current Program State
	Input:			thread_control_block   - interpreter info
	Input:			state                  - Program State
	Return: 		NULL
*************************************************/ 
void setPrgmState(struct thread_control_block * objThdCtrlBlockPtr, InterpreterState state)
{
    FST_INFO("setPrgmState to %d at %d", (int)state, objThdCtrlBlockPtr->is_main_thread);
	if(objThdCtrlBlockPtr->is_main_thread == MAIN_THREAD)
	{
	 	g_privateInterpreterState = state ;
		g_interpreter_publish.status = state ;
	}
	else
	{
		FST_INFO("setPrgmState Failed to %d", (int)state);
	}
}

/************************************************* 
	Function:		getProgMode
	Description:	get current Program Mode
	Input:			NULL
	Return: 		current Program Mode
*************************************************/ 
ProgMode getProgMode(struct thread_control_block * objThdCtrlBlockPtr)
{
	return objThdCtrlBlockPtr->prog_mode ;
}

/************************************************* 
	Function:		setPrgmState
	Description:	set current Program State
	Input:			thread_control_block   - interpreter info
	Input:			state                  - Program State
	Return: 		NULL
*************************************************/ 
void setProgMode(struct thread_control_block * objThdCtrlBlockPtr, ProgMode progMode)
{
    FST_INFO("setProgMode to %d at %d", (int)progMode, objThdCtrlBlockPtr->is_main_thread);
 	objThdCtrlBlockPtr->prog_mode   = progMode ;
}

void AddUseDefinedStructure(struct thread_control_block * objThreadCntrolBlock, 
							std::string structName, vector<eval_struct_member> vecMembers)
{
	objThreadCntrolBlock->mapUseDefinedStructType.insert(
		std::pair<std::string, vector<eval_struct_member> >(structName, vecMembers));
}

void AddUseDefinedStructureVar(struct thread_control_block * objThreadCntrolBlock, 
							   std::string varName, std::string structName)
{
	std::map<std::string, vector<eval_struct_member> >::iterator it
		= objThreadCntrolBlock->mapUseDefinedStructType.find(structName);
	if (it==objThreadCntrolBlock->mapUseDefinedStructType.end())
		return ;
    else
    {
		var_type   varType ;
		eval_struct_var objStructVar ;
		vector<eval_struct_var> vecVariable ;

		vector<eval_struct_member> objSecond = it->second ;
		for(vector<eval_struct_member>::iterator itMembers
			= objSecond.begin();
			itMembers != objSecond.end(); ++itMembers)
		{
			objStructVar.memberName    = itMembers->memberName ;
			objStructVar.memberTypeStr = itMembers->memberTypeStr ;
			vecVariable.push_back(objStructVar);
		}
		strcpy(varType.var_name, varName.c_str());
		// vector<eval_struct_var>
		varType.value.setStructData(vecVariable);
		objThreadCntrolBlock->global_vars.push_back(varType);
	}
}
/************************************************* 
	Function:		setCurLine
	Description:	set current Line info
	Input:			thread_control_block   - interpreter info
	Input:			line                   - Line XPath
	Input:			lineNum                - Line number
	Return: 		NULL
*************************************************/ 
void setCurLine(struct thread_control_block * objThdCtrlBlockPtr, char * line, int lineNum)
{
	if(objThdCtrlBlockPtr->is_main_thread == MAIN_THREAD)
	{
		if(objThdCtrlBlockPtr->is_in_macro == false)
		{
			strcpy(g_interpreter_publish.current_line_path, line); 
			g_interpreter_publish.current_line_num = lineNum; 
		}
		else
		{
			FST_INFO("setCurLine Failed to %d in the macro", (int)lineNum);
		}
	}
	else
	{
		FST_INFO("setCurLine Failed to %d", (int)lineNum);
	}
}

/************************************************* 
	Function:		setWarning
	Description:	upload Error code
	Input:			warn                   - Error code
	Return: 		NULL
*************************************************/ 
#ifdef WIN32
void setWarning(__int64 warn)
#else
void setWarning(long long int warn)
#endif  
{
#ifndef WIN32
	// g_objInterpreterServer->sendEvent(INTERPRETER_EVENT_TYPE_ERROR, &warn);
	fst_base::ErrorMonitor::instance()->add(warn);
#endif  
}

/************************************************* 
	Function:		setMessage
	Description:	upload Message code
	Input:			warn                   - Message code
	Return: 		NULL
*************************************************/ 
void setMessage(int warn)
{
#ifndef WIN32
	// g_objInterpreterServer->sendEvent(INTERPRETER_EVENT_TYPE_MESSAGE, &warn);
	fst_base::ErrorMonitor::instance()->add(warn);
#endif  
}

/************************************************* 
	Function:		setInstruction
	Description:	Send MOV* Instruction to controller
	Input:			thread_control_block   - interpreter info
	Input:			instruction            - Instruction Object
	Return: 		true - success ; false - failed
*************************************************/ 
bool setInstruction(struct thread_control_block * objThdCtrlBlockPtr, Instruction * instruction)
{
    bool ret = true;
//    int iLineNum = 0;
	// We had eaten MOV* as token. 
    if (objThdCtrlBlockPtr->is_abort)
    {
        return false;
    }

#ifndef WIN32
    do
    {
		if (instruction->is_additional == false)
		{
	     	FST_INFO("setInstruction:: instr.target.cnt = %f .", instruction->target.cnt);
			state_machine_ptr_->getNewInstruction(instruction);
		}
		else
		{
	     	FST_INFO("setInstruction:: instr.target.cnt = %f .", instruction->target.cnt);
			state_machine_ptr_->getNewInstruction(instruction);
		}
		
//	    if (ret)
	    {
//	        if (is_backward)
//	        {
//	            is_backward = false;
//		        //    iLineNum--;
//		        //    setCurLine(iLineNum);
//	        }
	        //else
	        //{
	        //    iLineNum++;
	        //    setCurLine(iLineNum);
	        //}   
			//	iLineNum = calc_line_from_prog(objThdCtrlBlockPtr);
			//  FST_INFO("set line in setInstruction");
	        //    setLinenum(objThdCtrlBlockPtr, iLineNum);

	        if (objThdCtrlBlockPtr->prog_mode == STEP_MODE)
	        {
			    FST_INFO("In STEP_MODE, it seems that it does not need to wait");
	            // setPrgmState(objThreadCntrolBlock, INTERPRETER_EXECUTE_TO_PAUSE);   //wait until this Instruction end
            }
	    }
        usleep(1000);
     //   if (count++ > 500)
     //       return false;
    }while(!ret);
	
    ret = state_machine_ptr_->isNextInstructionNeeded();
    FST_INFO("wait ctrl_status.is_permitted == false");
    while (ret == false)
    {
        usleep(1000);
    	ret = state_machine_ptr_->isNextInstructionNeeded();
    }
    FST_INFO("ctrl_status.is_permitted == true");
#endif

    return true;
}
	
void dealCodeStart(int program_code)
{
	struct thread_control_block * objThdCtrlBlockPtr ;
	g_launch_code_mgr_ptr->updateAll();
	std::string program_name = g_launch_code_mgr_ptr->getProgramByCode(program_code);
	if(program_name != "")
	{
        FST_INFO("start run %s ...", program_name.c_str());
		if(strcmp(getProgramName(), program_name.c_str()) == 0)
        {
        	FST_INFO("Duplicate to trigger and omit it while %s is executing ...", program_name.c_str());
        	return;
		}
		incCurrentThreadSeq();
	    // objThdCtrlBlockPtr = &g_thread_control_block[getCurrentThreadSeq()];
	    objThdCtrlBlockPtr = getThreadControlBlock();
		if(objThdCtrlBlockPtr == NULL) return ;
		// Clear last lineNum
		setCurLine(objThdCtrlBlockPtr, (char *)"", 0);
		
//      objThdCtrlBlockPtr->prog_mode   = FULL_MODE;
//  	g_interpreter_publish.progMode  = FULL_MODE;
  		setProgMode(objThdCtrlBlockPtr, FULL_MODE);
		objThdCtrlBlockPtr->execute_direction = EXECUTE_FORWARD ;
		startFile(objThdCtrlBlockPtr, 
			(char *)program_name.c_str(), getCurrentThreadSeq());
        setPrgmState(objThdCtrlBlockPtr, INTERPRETER_EXECUTE);
	}
	else 
	{
  		setWarning(FAIL_INTERPRETER_FILE_NOT_FOUND); 
	}
}

/************************************************* 
	Function:		startFile
	Description:	start running Program File
	Input:			thread_control_block   - interpreter info
	Input:			proj_name              - Program File name
	Input:			idx                    - Thread Index
	Return: 		NULL
*************************************************/ 
void startFile(struct thread_control_block * objThdCtrlBlockPtr, 
	char * proj_name, int idx)
{
	if(strlen(proj_name) < LAB_LEN)
	{
		strcpy(objThdCtrlBlockPtr->project_name, proj_name); // "prog_demo_dec"); // "BAS-EX1.BAS") ; // 
	}
	else
	{
		serror(objThdCtrlBlockPtr, 23);
		return;
	}
	// Just set to default value and it will change in the append_program_prop_mapper
	objThdCtrlBlockPtr->is_main_thread = MAIN_THREAD ;
	objThdCtrlBlockPtr->is_in_macro    = false ;
	objThdCtrlBlockPtr->iThreadIdx = idx ;
	// Refresh InterpreterPublish project_name
	
	setProgramName(objThdCtrlBlockPtr, proj_name); 
	setCurLine(objThdCtrlBlockPtr, "", 0);
	// Start thread
	basic_thread_create(idx, objThdCtrlBlockPtr);
	// intprt_ctrl.cmd = LOAD ;
}

/************************************************* 
	Function:		waitInterpreterStateleftPaused
	Description:	wait Interpreter State left Paused
	Input:			thread_control_block   - interpreter info
	Return: 		NULL
*************************************************/ 
void waitInterpreterStateleftPaused(
	struct thread_control_block * objThdCtrlBlockPtr)
{
	InterpreterState interpreterState  = getPrgmState();
	while(interpreterState == INTERPRETER_PAUSED)
	{
#ifdef WIN32
		Sleep(100);
		interpreterState = INTERPRETER_EXECUTE ;
#else
		sleep(1);
		interpreterState = getPrgmState();
		if(objThdCtrlBlockPtr->is_abort == true)
		{
			// setPrgmState(objThreadCntrolBlock, INTERPRETER_PAUSE_TO_IDLE) ;
			break;
		}
#endif
	}
}

/************************************************* 
	Function:		waitInterpreterStateleftPaused
	Description:	wait Interpreter State enter Paused
	Input:			thread_control_block   - interpreter info
	Return: 		NULL
*************************************************/ 
void waitInterpreterStateToPaused(
	struct thread_control_block * objThdCtrlBlockPtr)
{
	InterpreterState interpreterState  = getPrgmState();
	while(interpreterState != INTERPRETER_PAUSED)
	{
#ifdef WIN32
		Sleep(100);
		// interpreterState = INTERPRETER_EXECUTE ;
#else
		sleep(1);
		interpreterState = getPrgmState();
		if(objThdCtrlBlockPtr->is_abort == true)
		{
			// setPrgmState(objThreadCntrolBlock, INTERPRETER_PAUSE_TO_IDLE) ;
   			FST_INFO("waitInterpreterStateToPaused abort");
			break;
		}
		if(interpreterState == INTERPRETER_IDLE)
		{
   			FST_INFO("waitInterpreterStateToPaused = IDLE_R");
			break;
		}
#endif
	}
}	

/************************************************* 
	Function:		parseCtrlComand
	Description:	parse Controller Command
	Input:			intprt_ctrl            - Command
	Input:			requestDataPtr         - Command parameters
	Return: 		NULL
*************************************************/ 
void parseCtrlComand(InterpreterControl intprt_ctrl, char * requestDataPtr) 
	// (struct thread_control_block * objThdCtrlBlockPtr)
{
//	InterpreterState interpreterState  = IDLE_R;
	int iLineNum = 0 ;
#ifdef WIN32
	__int64 result = 0 ;
	static int lastCmd ;
#else
	static fst_base::InterpreterServerCmd lastCmd ;
#endif
//	UserOpMode userOpMode ;
	int        program_code ;
    thread_control_block * objThdCtrlBlockPtr = NULL;

	g_launch_code_mgr_ptr->updateAll();
#ifndef WIN32
//	RegMap reg ;
//	IOPathInfo  dioPathInfo ;
	// if(intprt_ctrl.cmd != UPDATE_IO_DEV_ERROR)
	if(intprt_ctrl.cmd != fst_base::INTERPRETER_SERVER_CMD_LOAD)
        FST_INFO("parseCtrlComand: %d", intprt_ctrl.cmd);
#endif
    switch (intprt_ctrl.cmd)
    {
        case fst_base::INTERPRETER_SERVER_CMD_LOAD:
            // FST_INFO("load file_name");
            break;
        case fst_base::INTERPRETER_SERVER_CMD_LAUNCH:
			memcpy(intprt_ctrl.start_ctrl, requestDataPtr, 256);
            FST_INFO("start debug %s ...", intprt_ctrl.start_ctrl);
			if(strcmp(getProgramName(), intprt_ctrl.start_ctrl) == 0)
            {
            	FST_INFO("Duplicate to LAUNCH %s ...", intprt_ctrl.start_ctrl);
				if(getPrgmState() == INTERPRETER_IDLE)
				{
            		FST_INFO("Duplicate to LAUNCH %s at INTERPRETER_IDLE ...", 
						intprt_ctrl.start_ctrl);
					strcpy(g_interpreter_publish.program_name, ""); 
				}
				else
				{
            		FST_INFO("Duplicate to LAUNCH %s at %d ...", 
						intprt_ctrl.start_ctrl, (int)getPrgmState());
				    setWarning(FAIL_INTERPRETER_DUPLICATE_LAUNCH) ; 
            		break;
				}
			}
			incCurrentThreadSeq();
		    // objThdCtrlBlockPtr = &g_thread_control_block[getCurrentThreadSeq()];
		    objThdCtrlBlockPtr = getThreadControlBlock();
			if(objThdCtrlBlockPtr == NULL) break ;
			// Clear last lineNum
			setCurLine(objThdCtrlBlockPtr, (char *)"", 0);
			
  			setProgMode(objThdCtrlBlockPtr, STEP_MODE);
			objThdCtrlBlockPtr->execute_direction = EXECUTE_FORWARD ;
			if(strlen(intprt_ctrl.start_ctrl) == 0)
			{
			   strcpy(intprt_ctrl.start_ctrl, "lineno_test_2");
			}
            startFile(objThdCtrlBlockPtr, 
				intprt_ctrl.start_ctrl, getCurrentThreadSeq());
            setPrgmState(objThdCtrlBlockPtr, INTERPRETER_PAUSED);
	        // g_iCurrentThreadSeq++ ;
            break;
        case fst_base::INTERPRETER_SERVER_CMD_START:
			memcpy(intprt_ctrl.start_ctrl, requestDataPtr, 256);
            FST_INFO("start run %s ...", intprt_ctrl.start_ctrl);
			if(strcmp(getProgramName(), intprt_ctrl.start_ctrl) == 0)
            {
            	FST_INFO("Duplicate to START %s ...", intprt_ctrl.start_ctrl);
				
            	FST_INFO("Duplicate to LAUNCH %s ...", intprt_ctrl.start_ctrl);
				if(getPrgmState() == INTERPRETER_IDLE)
				{
            		FST_INFO("Duplicate to LAUNCH %s at INTERPRETER_IDLE ...", 
						intprt_ctrl.start_ctrl);
					strcpy(g_interpreter_publish.program_name, ""); 
				}
				else
				{
            		FST_INFO("Duplicate to LAUNCH %s at %d ...", 
						intprt_ctrl.start_ctrl, (int)getPrgmState());
				    setWarning(FAIL_INTERPRETER_DUPLICATE_START) ; 
            		break;
				}
			}
			incCurrentThreadSeq();
		    // objThdCtrlBlockPtr = &g_thread_control_block[getCurrentThreadSeq()];
		    objThdCtrlBlockPtr = getThreadControlBlock();
			if(objThdCtrlBlockPtr == NULL) break ;
			// Clear last lineNum
			setCurLine(objThdCtrlBlockPtr, (char *)"", 0);
			
       //     objThdCtrlBlockPtr->prog_mode   = FULL_MODE;
  	   //	  g_interpreter_publish.progMode  = FULL_MODE;
  			setProgMode(objThdCtrlBlockPtr, FULL_MODE);
			objThdCtrlBlockPtr->execute_direction = EXECUTE_FORWARD ;
			if(strlen(intprt_ctrl.start_ctrl) == 0)
			{
			   strcpy(intprt_ctrl.start_ctrl, "lineno_test_2");
			}
			startFile(objThdCtrlBlockPtr, 
				intprt_ctrl.start_ctrl, getCurrentThreadSeq());
            setPrgmState(objThdCtrlBlockPtr, INTERPRETER_EXECUTE);
	        // g_iCurrentThreadSeq++ ;
            break;
        case fst_base::INTERPRETER_SERVER_CMD_JUMP:
			memcpy(intprt_ctrl.jump_line, requestDataPtr, 256);
			if(getCurrentThreadSeq() < 0) break ;
			if(g_basic_interpreter_handle[getCurrentThreadSeq()] == 0)
			{
            	FST_ERROR("Thread exits at %d ", getPrgmState());
				break;
			}
			// objThdCtrlBlockPtr = &g_thread_control_block[getCurrentThreadSeq()];
		    objThdCtrlBlockPtr = getThreadControlBlock();
			if(objThdCtrlBlockPtr == NULL) break ;
			if(objThdCtrlBlockPtr->is_in_macro == true)
			{
				FST_ERROR("Can not JUMP macro ");
				break;
			}
//			if(objThdCtrlBlockPtr->is_paused == true)
//			{
//           	FST_ERROR("Can not JUMP in calling Pause ");
//         		break;
//			}
			if(getPrgmState() == INTERPRETER_EXECUTE)
			{
            	FST_ERROR("Can not JUMP in EXECUTE_R ");
           		break;
			}
			iLineNum = getLineNumFromXPathVector(objThdCtrlBlockPtr, intprt_ctrl.jump_line);
            FST_INFO("jump to line:%s return %d", intprt_ctrl.jump_line, iLineNum);
			if(iLineNum > 0)
            {
            	setLinenum(objThdCtrlBlockPtr, iLineNum);
				// Jump prog
				jump_prog_from_line(objThdCtrlBlockPtr, iLineNum);
				// Just Move to line and do not execute
            	// setPrgmState(objThdCtrlBlockPtr, INTERPRETER_EXECUTE);
			}
			else
            {
            	FST_ERROR("Failed to jump to line:%d", iLineNum);
				setWarning(FAIL_INTERPRETER_ILLEGAL_LINE_NUMBER);
			}
			break;
        case fst_base::INTERPRETER_SERVER_CMD_FORWARD:
            FST_INFO("step forward at %d ", getCurrentThreadSeq());
			if(getCurrentThreadSeq() < 0) break ;
			if(g_basic_interpreter_handle[getCurrentThreadSeq()] == 0)
			{
            	FST_ERROR("Thread exits at %d ", getPrgmState());
				break;
			}
			// objThdCtrlBlockPtr = &g_thread_control_block[getCurrentThreadSeq()];
		    objThdCtrlBlockPtr = getThreadControlBlock();
			if(objThdCtrlBlockPtr == NULL) break ;
			if(objThdCtrlBlockPtr->is_in_macro == true)
			{
				FST_ERROR("Can not FORWARD macro ");
				break;
			}
//			if(objThdCtrlBlockPtr->is_paused == true)
//			{
//            	FST_ERROR("Can not FORWARD in calling Pause ");
//           		break;
//			}
			if(getPrgmState() == INTERPRETER_EXECUTE)
			{
            	FST_ERROR("Can not FORWARD in EXECUTE_R ");
           		break;
			}
	        // Checking prog_mode on 190125 
			if(objThdCtrlBlockPtr->prog_mode != STEP_MODE)
			{
            //	FST_ERROR("Can not FORWARD in other mode ");
            	FST_INFO("Using FORWARD would switch mode ");
  			    setProgMode(objThdCtrlBlockPtr, STEP_MODE);
			}
			
            // objThdCtrlBlockPtr->prog_mode = STEP_MODE ;
			objThdCtrlBlockPtr->execute_direction = EXECUTE_FORWARD ;

			iLineNum = calc_line_from_prog(objThdCtrlBlockPtr);
            setLinenum(objThdCtrlBlockPtr, iLineNum);
            FST_INFO("step forward to %d ", iLineNum);
            setPrgmState(objThdCtrlBlockPtr, INTERPRETER_EXECUTE);

			// Controller use the PrgmState and LineNum to check to execute 
//            FST_INFO("Enter waitInterpreterStateToPaused %d ", iLineNum);
//            waitInterpreterStateToPaused(objThdCtrlBlockPtr);

			// Use the program pointer to get the current line number.
			// to support logic
            break;
        case fst_base::INTERPRETER_SERVER_CMD_BACKWARD:
            FST_INFO("backward at %d ", getCurrentThreadSeq());
			if(getCurrentThreadSeq() < 0) break ;
			if(g_basic_interpreter_handle[getCurrentThreadSeq()] == 0)
			{
            	FST_ERROR("Thread exits at %d ", getPrgmState());
				break;
			}
			// objThdCtrlBlockPtr = &g_thread_control_block[getCurrentThreadSeq()];
		    objThdCtrlBlockPtr = getThreadControlBlock();
			if(objThdCtrlBlockPtr == NULL) break ;
			if(objThdCtrlBlockPtr->is_in_macro == true)
			{
				FST_ERROR("Can not BACKWARD macro ");
				break;
			}
//			if(objThdCtrlBlockPtr->is_paused == true)
//			{
//            	FST_ERROR("Can not BACKWARD in calling Pause ");
//         		break;
//			}
			if(getPrgmState() == INTERPRETER_EXECUTE)
			{
            	FST_ERROR("Can not BACKWARD in EXECUTE_R ");
           		break;
			}
	        // Checking prog_mode on 190125 
			if(objThdCtrlBlockPtr->prog_mode != STEP_MODE)
			{
            //	FST_ERROR("Can not BACKWARD in other mode ");
            	FST_INFO("Using FORWARD would switch mode ");
  			    setProgMode(objThdCtrlBlockPtr, STEP_MODE);
           	//	break;
			}
			// Revert checking condition on 190125
			// if(lastCmd == fst_base::INTERPRETER_SERVER_CMD_FORWARD)
			if(lastCmd != fst_base::INTERPRETER_SERVER_CMD_BACKWARD)
			{
			    // In this circumstance, 
			    // we need not call calc_line_from_prog to get the next FORWARD line.
				// Just execute last statemant
			    iLineNum = getLinenum(objThdCtrlBlockPtr);
				// SWitch prog_mode and execute_direction on 190125
                // objThdCtrlBlockPtr->prog_mode = STEP_MODE ;
			    objThdCtrlBlockPtr->execute_direction = EXECUTE_BACKWARD ;
			
				if((objThdCtrlBlockPtr->prog_jmp_line[iLineNum - 1].type == LOGIC_TOK)
				 ||(objThdCtrlBlockPtr->prog_jmp_line[iLineNum - 1].type == END_TOK))
				{
            		FST_ERROR("Can not BACKWARD to %d(%d).",
						iLineNum, objThdCtrlBlockPtr->prog_jmp_line[iLineNum].type);
				    setWarning(INFO_INTERPRETER_BACK_TO_LOGIC) ;
					break ;
				}
				// In fact, It does nothing
				// iLineNum-- ;
		    	setLinenum(objThdCtrlBlockPtr, iLineNum);
            	FST_INFO("JMP to %d(%d) in the FORWARD -> BACKWARD .", 
					iLineNum,    objThdCtrlBlockPtr->prog_jmp_line[iLineNum].type);
                setPrgmState(objThdCtrlBlockPtr, INTERPRETER_EXECUTE);
				break;
			}
            // setPrgmState(objThdCtrlBlockPtr, INTERPRETER_EXECUTE);  
			// In this circumstance, 
			// We had jmp to the right line, we should use the iLineNum.
            iLineNum = getLinenum(objThdCtrlBlockPtr);
            if (iLineNum < PROGRAM_START_LINE_NUM)
            {
            	FST_ERROR("Can not BACKWARD out of program ");
  				setWarning(INFO_INTERPRETER_BACK_TO_BEGIN) ; 
                break;
            }
            // if (objThdCtrlBlockPtr->prog_jmp_line[iLineNum].type == MOTION)
//            is_backward = true;
            // else {  perror("can't back");  break;      }
            // objThdCtrlBlockPtr->prog_mode = STEP_MODE ;
			objThdCtrlBlockPtr->execute_direction = EXECUTE_BACKWARD ;
            FST_INFO("step BACKWARD to %d ", iLineNum);
			// set_prog_from_line(objThdCtrlBlockPtr, iLineNum);
			iLineNum-- ;
			
			if((objThdCtrlBlockPtr->prog_jmp_line[iLineNum - 1].type == LOGIC_TOK)
			 ||(objThdCtrlBlockPtr->prog_jmp_line[iLineNum - 1].type == END_TOK))
			{
				FST_ERROR("Can not BACKWARD to %d(%d).",
					iLineNum, objThdCtrlBlockPtr->prog_jmp_line[iLineNum].type);
				break ;
			}
		    setLinenum(objThdCtrlBlockPtr, iLineNum);
            setPrgmState(objThdCtrlBlockPtr, INTERPRETER_EXECUTE);

			// Controller use the PrgmState and LineNum to check to execute 
//            FST_INFO("Enter waitInterpreterStateToPaused %d ", iLineNum);
//			waitInterpreterStateToPaused(objThdCtrlBlockPtr);
		    break;
		case fst_base::INTERPRETER_SERVER_CMD_RESUME:
			if(getCurrentThreadSeq() < 0) break ;
		    // objThdCtrlBlockPtr = &g_thread_control_block[getCurrentThreadSeq()];
		    objThdCtrlBlockPtr = getThreadControlBlock();
			if(objThdCtrlBlockPtr == NULL) break ;
			
			if(getPrgmState() == INTERPRETER_PAUSED)
	        {
	            FST_INFO("continue move..");
				// Not Change program mode  
				// objThdCtrlBlockPtr->prog_mode = FULL_MODE;
	            setPrgmState(objThdCtrlBlockPtr, INTERPRETER_EXECUTE);
  			    setProgMode(objThdCtrlBlockPtr, FULL_MODE);
			}
			else
	        {
			    setWarning(FAIL_INTERPRETER_NOT_IN_PAUSE);
			}
            break;
        case fst_base::INTERPRETER_SERVER_CMD_PAUSE:
			if(getCurrentThreadSeq() < 0) break ;
			// objThdCtrlBlockPtr = &g_thread_control_block[getCurrentThreadSeq()];
		    objThdCtrlBlockPtr = getThreadControlBlock(false);
			if(objThdCtrlBlockPtr == NULL) break ;
			if(objThdCtrlBlockPtr->is_in_macro == true)
			{
				FST_ERROR("Can not PAUSE macro ");
				break;
			}
			if(getPrgmState() == INTERPRETER_IDLE)
			{
            	FST_ERROR("Can not PAUSE in INTERPRETER_IDLE ");
           		break;
			}
			
//            userOpMode = getUserOpMode();
//            if ((userOpMode == SLOWLY_MANUAL_MODE_U)
//            || (userOpMode == UNLIMITED_MANUAL_MODE_U))
//            {
//                objThdCtrlBlockPtr->prog_mode = STEP_MODE ;
//            }
            setPrgmState(objThdCtrlBlockPtr, INTERPRETER_PAUSED); 
  			setProgMode(objThdCtrlBlockPtr, STEP_MODE);
            break;
        case fst_base::INTERPRETER_SERVER_CMD_ABORT:
            FST_ERROR("abort motion");
			if(getCurrentThreadSeq(false) < 0) break ;
		    // objThdCtrlBlockPtr = &g_thread_control_block[getCurrentThreadSeq()];
		    objThdCtrlBlockPtr = getThreadControlBlock(false);
			if(objThdCtrlBlockPtr == NULL) break ;
			
  			FST_INFO("set abort motion flag.");
	        objThdCtrlBlockPtr->is_abort = true;
            // Restore program pointer
  			// FST_INFO("reset prog position.");
            // objThdCtrlBlockPtr->prog = objThdCtrlBlockPtr->p_buf ;

 		    setPrgmState(objThdCtrlBlockPtr, INTERPRETER_PAUSE_TO_IDLE);
#ifdef WIN32
 			Sleep(1);
#else
	        usleep(1000);
#endif
 			FST_INFO("setPrgmState(IDLE_R).");
		    setPrgmState(objThdCtrlBlockPtr, INTERPRETER_IDLE);

		    // clear line path and ProgramName
  			FST_INFO("reset ProgramName And LineNum.");
			// Keep LineNum and Line xPath
			// resetProgramNameAndLineNum(objThdCtrlBlockPtr);
			// Clear here will get a bug .
			// setProgramName(objThdCtrlBlockPtr, (char *)""); 
            break;
        case fst_base::INTERPRETER_SERVER_CMD_CODE_START:
			memcpy(&intprt_ctrl.program_code, requestDataPtr, sizeof(AutoMode));
			// intprt_ctrl.RegMap.
			program_code = intprt_ctrl.program_code ;
			// Move to Controller
			dealCodeStart(program_code);
            break;
        default:
            break;

    }
	lastCmd = intprt_ctrl.cmd;
  	//		FST_INFO("left parseCtrlComand.");
}

/************************************************* 
	Function:		forgesight_load_programs_path
	Description:	load programs path
	Input:			NULL
	Return: 		NULL
*************************************************/ 
void forgesight_load_programs_path()
{
	std::string data_path = "";
	g_files_manager_data_path = "";
#ifdef WIN32
	TCHAR pBuf[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, pBuf);
    g_files_manager_data_path = std::string(pBuf) + std::string(DATA_PATH);
#else
	if(getenv("ROBOT_DATA_PREFIX") != NULL)
		g_files_manager_data_path = string(getenv("ROBOT_DATA_PREFIX"));
	
	if(g_files_manager_data_path.length() == 0)
	{
	    fst_parameter::ParamGroup param_;
	    param_.loadParamFile("/root/install/share/runtime/component_param/programs_path.yaml");
	    param_.getParam("file_manager/programs_path", g_files_manager_data_path);
	}
	else
	{
	    fst_parameter::ParamGroup param_;
	    param_.loadParamFile("/root/install/share/runtime/component_param/programs_path.yaml");
	    param_.getParam("file_manager/data_path", data_path);
		g_files_manager_data_path.append(data_path);
	}
	FST_INFO("forgesight_load_programs_path: %s .", g_files_manager_data_path.c_str());
#endif
	
}

/************************************************* 
	Function:		forgesight_get_programs_path
	Description:	get programs path
	Input:			NULL
	Return: 		programs path
*************************************************/ 
char * forgesight_get_programs_path()
{
	return (char *)g_files_manager_data_path.c_str();
}

/************************************************* 
	Function:		forgesight_load_wait_time_out_config
	Description:	load programs path
	Input:			NULL
	Return: 		NULL
*************************************************/ 
void forgesight_load_wait_time_out_config()
{
#ifdef WIN32
    g_wait_time_out_config = 10;
#else
    fst_parameter::ParamGroup param_;
    param_.loadParamFile("/root/install/share/runtime/component_param/prg_interpreter_config.yaml");
    param_.getParam("wait_time/time_out", g_wait_time_out_config);
	FST_INFO("forgesight_load_wait_time_out_config: %d .", g_wait_time_out_config);
	
	if(g_wait_time_out_config <= 0)
	    g_wait_time_out_config = MAX_WAIT_SECOND ;
#endif
	
}

/************************************************* 
	Function:		forgesight_get_programs_path
	Description:	get programs path
	Input:			NULL
	Return: 		programs path
*************************************************/ 
int forgesight_get_wait_time_out_config()
{
	return g_wait_time_out_config;
}

/************************************************* 
	Function:		initInterpreter
	Description:	init interpretor
	Input:			NULL
	Return: 		programs path
*************************************************/ 
void initInterpreter()
{
	xmlInitParser();
	g_privateInterpreterState = INTERPRETER_IDLE ;
	
	forgesight_load_programs_path();
	forgesight_load_wait_time_out_config();
	g_launch_code_mgr_ptr = new LaunchCodeMgr(g_files_manager_data_path);
	g_home_pose_mgr_ptr   = new HomePoseMgr(g_files_manager_data_path);
#ifdef WIN32
	import_external_resource("config\\user_defined_variable.xml", g_vecKeyVariables);
#else
	import_external_resource(
		"/root/install/share/runtime/interpreter/user_defined_variable.xml", 
		g_vecKeyVariables);
#endif
	for(int iIdx = 0; iIdx < NUM_THREAD + 1; iIdx++)
	{
		g_basic_interpreter_handle[iIdx] = NULL;
	}
}

/************************************************* 
	Function:		forgesight_get_programs_path
	Description:	get programs path
	Input:			NULL
	Return: 		programs path
*************************************************/ 
bool forgesight_find_external_resource(char *vname, key_variable& keyVar)
{
	// Otherwise, try global vars.
	for(unsigned i=0; i < g_vecKeyVariables.size(); i++)
	{
	//	FST_INFO("forgesight_find_external_resource: %s .", g_vecKeyVariables[i].key_name);
		if(!strcmp(g_vecKeyVariables[i].key_name, vname)) {
			keyVar = g_vecKeyVariables[i] ;
			return true;
		}
	}
	return false;
}


/************************************************* 
	Function:		forgesight_get_programs_path
	Description:	get programs path
	Input:			NULL
	Return: 		programs path
*************************************************/ 
bool forgesight_find_external_resource_by_xmlname(
					char *xml_name, key_variable& keyVar)
{
	// Otherwise, try global vars.
	for(unsigned i=0; i < g_vecKeyVariables.size(); i++)
	{
	//	FST_INFO("forgesight_find_external_resource: %s .", g_vecKeyVariables[i].key_name);
		if(!strcmp(g_vecKeyVariables[i].xml_name, xml_name)) {
			keyVar = g_vecKeyVariables[i] ;
			return true;
		}
	}
	return false;
}

void updateHomePoseMgr()
{
	g_home_pose_mgr_ptr->updateAll();
}

checkHomePoseResult checkSingleHomePoseByCurrentJoint(int idx, Joint currentJoint)
{
	Joint joint      = g_home_pose_mgr_ptr->homePoseList[idx].joint ;
	Joint jointFloat = g_home_pose_mgr_ptr->homePoseList[idx].jointFloat ;
	
#ifndef WIN32
		printf("Get JOINT: %d :: (%f, %f, %f, %f, %f, %f, %f, %f, %f) \n", idx, 
			joint.j1_, joint.j2_, joint.j3_, 
			joint.j4_, joint.j5_, joint.j6_,  
			joint.j7_, joint.j8_, joint.j9_);
		printf("with jointFloat:(%f, %f, %f, %f, %f, %f, %f, %f, %f) \n", 
			jointFloat.j1_, jointFloat.j2_, jointFloat.j3_, 
			jointFloat.j4_, jointFloat.j5_, jointFloat.j6_, 
			jointFloat.j7_, jointFloat.j8_, jointFloat.j9_);
		printf("Get currentJoint: (%f, %f, %f, %f, %f, %f, %f, %f, %f) \n", 
			currentJoint.j1_, currentJoint.j2_, currentJoint.j3_, 
			currentJoint.j4_, currentJoint.j5_, currentJoint.j6_,  
			currentJoint.j7_, currentJoint.j8_, currentJoint.j9_);
#else
		printf("Get JOINT: %d :: (%f, %f, %f, %f, %f, %f, %f, %f, %f) \n", idx, 
			joint.j1, joint.j2, joint.j3, 
			joint.j4, joint.j5, joint.j6,  
			joint.j7, joint.j8, joint.j9);
		printf("with jointFloat:(%f, %f, %f, %f, %f, %f, %f, %f, %f) \n", 
			jointFloat.j1, jointFloat.j2, jointFloat.j3, 
			jointFloat.j4, jointFloat.j5, jointFloat.j6, 
			jointFloat.j7, jointFloat.j8, jointFloat.j9);
		printf("Get currentJoint: (%f, %f, %f, %f, %f, %f, %f, %f, %f) \n", 
			currentJoint.j1, currentJoint.j2, currentJoint.j3, 
			currentJoint.j4, currentJoint.j5, currentJoint.j6,  
			currentJoint.j7, currentJoint.j8, currentJoint.j9);
#endif
	return g_home_pose_mgr_ptr->checkSingleHomePoseByJoint(idx, currentJoint);
}

/************************************************* 
	Function:		uninitInterpreter
	Description:	uninit interpretor
	Input:			NULL
	Return: 		programs path
*************************************************/ 
void uninitInterpreter()
{
	delete g_launch_code_mgr_ptr;
	delete g_home_pose_mgr_ptr;
	xmlCleanupParser();
}

/************************************************* 
	Function:		updateIOError (Legacy)
	Description:	update IO Error
	Input:			NULL
	Return: 		NULL
*************************************************/ 
void updateIOError()
{
#ifndef WIN32
//	U64 result = SUCCESS ;
//	result = IOInterface::instance()->updateIOError();
//    if (result != SUCCESS)
//    {
//		setWarning(result) ; 
//    }
#endif
}

