SUB main ()
	DO[9] = OFF
	DO[8] = ON
	WAIT COND DO[0]  = ON
	WAIT COND DO[1]  = OFF
	WAIT 30
	WAIT COND DO[0]  = OFF
	WAIT COND DO[1]  = OFF
END SUB 