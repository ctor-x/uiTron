/*   This is originally based on an m68k software stub written by Glenn
     Engel at HP, but has changed quite a bit. 

     Modifications for the SH by Ben Lee and Steve Chamberlain

*/

/****************************************************************************

		THIS SOFTWARE IS NOT COPYRIGHTED

   HP offers the following for use in the public domain.  HP makes no
   warranty with regard to the software or it's performance and the
   user accepts the software "AS IS" with all faults.

   HP DISCLAIMS ANY WARRANTIES, EXPRESS OR IMPLIED, WITH REGARD
   TO THIS SOFTWARE INCLUDING BUT NOT LIMITED TO THE WARRANTIES
   OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.

****************************************************************************/


/* Remote communication protocol.

   A debug packet whose contents are <data>
   is encapsulated for transmission in the form:

	$ <data> # CSUM1 CSUM2

	<data> must be ASCII alphanumeric and cannot include characters
	'$' or '#'.  If <data> starts with two characters followed by
	':', then the existing stubs interpret this as a sequence number.

	CSUM1 and CSUM2 are ascii hex representation of an 8-bit 
	checksum of <data>, the most significant nibble is sent first.
	the hex digits 0-9,a-f are used.

   Receiver responds with:

	+	- if CSUM is correct and ready for next packet
	-	- if CSUM is incorrect

   <data> is as follows:
   All values are encoded in ascii hex digits.

	Request		Packet

	read registers  g
	reply		XX....X		Each byte of register data
					is described by two hex digits.
					Registers are in the internal order
					for GDB, and the bytes in a register
					are in the same order the machine uses.
			or ENN		for an error.

	write regs	GXX..XX		Each byte of register data
					is described by two hex digits.
	reply		OK		for success
			ENN		for an error

        write reg	Pn...=r...	Write register n... with value r...,
					which contains two hex digits for each
					byte in the register (target byte
					order).
	reply		OK		for success
			ENN		for an error
	(not supported by all stubs).

	read mem	mAA..AA,LLLL	AA..AA is address, LLLL is length.
	reply		XX..XX		XX..XX is mem contents
					Can be fewer bytes than requested
					if able to read only part of the data.
			or ENN		NN is errno

	write mem	MAA..AA,LLLL:XX..XX
					AA..AA is address,
					LLLL is number of bytes,
					XX..XX is data
	reply		OK		for success
			ENN		for an error (this includes the case
					where only part of the data was
					written).

	write mem	XAA..AA,LLLL:XX..XX
	 (binary)			AA..AA is address,
					LLLL is number of bytes,
					XX..XX is binary data
	reply		OK		for success
			ENN		for an error

	cont		cAA..AA		AA..AA is address to resume
					If AA..AA is omitted,
					resume at same address.

	step		sAA..AA		AA..AA is address to resume
					If AA..AA is omitted,
					resume at same address.

	last signal     ?               Reply the current reason for stopping.
                                        This is the same reply as is generated
					for step or cont : SAA where AA is the
					signal number.

	There is no immediate reply to step or cont.
	The reply comes when the machine stops.
	It is		SAA		AA is the "signal number"

	or...		TAAn...:r...;n:r...;n...:r...;
					AA = signal number
					n... = register number
					r... = register contents
	or...		WAA		The process exited, and AA is
					the exit status.  This is only
					applicable for certains sorts of
					targets.
	kill request	k

	toggle debug	d		toggle debug flag (see 386 & 68k stubs)
	reset		r		reset -- see sparc stub.
	reserved	<other>		On other requests, the stub should
					ignore the request and send an empty
					response ($#<checksum>).  This way
					we can extend the protocol and GDB
					can tell whether the stub it is
					talking to uses the old or the new.
	search		tAA:PP,MM	Search backwards starting at address
					AA for a match with pattern PP and
					mask MM.  PP and MM are 4 bytes.
					Not supported by all stubs.

	general query	qXXXX		Request info about XXXX.
	general set	QXXXX=yyyy	Set value of XXXX to yyyy.
	query sect offs	qOffsets	Get section offsets.  Reply is
					Text=xxx;Data=yyy;Bss=zzz
	console output	Otext		Send text to stdout.  Only comes from
					remote target.

	Responses can be run-length encoded to save space.  A '*' means that
	the next character is an ASCII encoding giving a repeat count which
	stands for that many repititions of the character preceding the '*'.
	The encoding is n+29, yielding a printable character where n >=3 
	(which is where rle starts to win).  Don't use an n > 126. 

	So 
	"0* " means the same as "0000".  */

#include "string.h"
#include "Interrupts.h"

// CCR bits --------------
#define CCR_C		0
#define CCR_V		1
#define CCR_Z		2
#define CCR_N		3

// instructions --------
#define OP_BRA_D8			0x40
#define OP_BRN_D8			0x41
#define OP_BHI_D8			0x42
#define OP_BLS_D8			0x43
#define OP_BCC_D8			0x44
#define OP_BCS_D8			0x45
#define OP_BNE_D8			0x46
#define OP_BEQ_D8			0x47
#define OP_BVC_D8			0x48
#define OP_BVS_D8			0x49
#define OP_BPL_D8			0x4A
#define OP_BMI_D8			0x4B
#define OP_BGE_D8			0x4C
#define OP_BLT_D8			0x4D
#define OP_BGT_D8			0x4E
#define OP_BLE_D8			0x4F
#define OP_RTS				0x54
#define OP_RTS_2			0x70
#define OP_BSR_D8			0x55
#define OP_RTE				0x56
#define OP_RTE_2			0x70
#define OP_TRAPA			0x57
#define OP_BCC_D16			0x58
#define OP_JMP_ER			0x59
#define OP_JMP_AA24			0x5A
#define OP_JMP_AA8			0x5B
#define OP_BSR_D16			0x5C
#define OP_BSR_D16_2		0x00
#define OP_JSR_ER			0x5D
#define OP_JSR_AA24			0x5E
#define OP_JSR_AA8			0x5F

#define BCC_D8_OP1_MASK		0x0F
#define BCC_D16_OP2_MASK	0xF0
#define JMP_ER_OP2_MASK		0x70
#define TRAPA_OP2_MASK		0x30

#define SSTEP_INSTR     0x5730		/* TRAPA #3 */

#define ADDR_24         0x00FFFFFF

/*
 * BUFMAX defines the maximum number of characters in inbound/outbound
 * buffers at least NUMREGBYTES*2 are needed for register packets
 */
#define BUFMAX 1024

/*
 * Number of bytes for registers
 */
#define NUMREGBYTES 40
#define NUMREGS NUMREGBYTES/4


extern void putDebugChar (char);
extern char getDebugChar (void);

/*
 * Forward declarations
 */
static char highhex(int);
static char lowhex(int);
static int hex (char);
static char *mem2hex (char *, char *, int);
static char *hex2mem (char *, char *, int);
static char *ebin2mem (char *, char *, int);
static int hex2int (char **, int *);

static void getpacket (char *);
static void putpacket (char *);
static char isBranchConditionMet(unsigned char);
static char computeInstrLen2(long *);
static char computeInstrLen(short *);
static int computeSignal (int);
static void doSStep(void);
static void undoSStep(void);
static void gdb_handle_exception(int);
static void handle_bios_call (void);

//#define stub_stack_size	64
//#define init_stack_size	320
//static int init_stack[init_stack_size]
//	__attribute__ ((section (".stack"))) = {0};
//int stub_stack[stub_stack_size]
//	__attribute__ ((section (".stack"))) = {0};

#define stub_stack_size	((0x500+0x100)/sizeof(int))
int stub_stack[stub_stack_size]
	__attribute__ ((section (".stack"))) = {0};

char in_nmi;   /* Set when handling an NMI, so we don't reenter */
int exception;

int *stub_sp;

/* debug > 0 prints ill-formed commands in valid packets & checksum errors */
static int remote_debug;

static int ingdbmode;

enum regnames
{
  R0,  R1, R2,  R3,  R4,   R5,   R6,  R7, 
  CCR,  PC
};

typedef struct
{
  short *memAddr;
  short oldInstr;
} stepData;

unsigned int registers[NUMREGS];
static stepData instrBuffer;
static char stepped;
static const char hexchars[] = "0123456789abcdef";
static char remcomInBuffer[BUFMAX];
static char remcomOutBuffer[BUFMAX];

/*-------------------------------------------------------------------------*/
static char
highhex(int  x)
{
  return hexchars[(x >> 4) & 0xf];
}

/*-------------------------------------------------------------------------*/
static char
lowhex(int  x)
{
  return hexchars[x & 0xf];
}

/*-------------------------------------------------------------------------*/
static int
hex (char ch)
{
  if ((ch >= 'a') && (ch <= 'f'))
    return (ch - 'a' + 10);
  if ((ch >= '0') && (ch <= '9'))
    return (ch - '0');
  if ((ch >= 'A') && (ch <= 'F'))
    return (ch - 'A' + 10);
  return (-1);
}

/*-------------------------------------------------------------------------*/
static char *
mem2hex (char *mem, char *buf, int count)
{
  int i;
  int ch;
  for (i = 0; i < count; i++)
  {
    ch = *mem++;
    *buf++ = highhex (ch);
    *buf++ = lowhex (ch);
  }
  *buf = 0;
  return (buf);
}

/*-------------------------------------------------------------------------*/
static char *
hex2mem (char *buf, char *mem, int count)
{
  int i;
  unsigned char ch;
  for (i = 0; i < count; i++)
  {
    ch = hex (*buf++) << 4;
    ch = ch + hex (*buf++);
    *mem++ = ch;
  }
  return (mem);
}

/*-------------------------------------------------------------------------*/
static char *
ebin2mem (char *buf, char *mem, int count)
{
  for ( ; count>0 ; count--, buf++)
  {
    if (*buf == 0x7d)
	  *mem++ = *(++buf) ^ 0x20;
    else
	  *mem++ = *buf;
  }
  return (mem);
}

/*-------------------------------------------------------------------------*/
static int
hex2int (char **ptr, int *intValue)
{
  int numChars = 0;
  int hexValue;

  *intValue = 0;

  while (**ptr)
  {
    hexValue = hex (**ptr);
    if (hexValue >= 0)
	{
	  *intValue = (*intValue << 4) | hexValue;
	  numChars++;
	}
    else
	break;

    (*ptr)++;
  }

  return (numChars);
}

/*-------------------------------------------------------------------------*/
static void
getpacket (char *buffer)
{
  unsigned char checksum;
  unsigned char xmitcsum;
  int i;
  int count;
  char ch;
  do
  {
    /* wait around for the start character, ignore all other characters */
    while ((ch = getDebugChar ()) != '$');
    checksum = 0;
    xmitcsum = -1;

    count = 0;

    /* now, read until a # or end of buffer is found */
    while (count < BUFMAX)
	{
	  ch = getDebugChar ();
	  if (ch == '#')
	    break;
	  checksum = checksum + ch;
	  buffer[count] = ch;
	  count = count + 1;
	}
    buffer[count] = 0;

    if (ch == '#')
	{
	  xmitcsum = hex (getDebugChar ()) << 4;
	  xmitcsum += hex (getDebugChar ());
	  if (checksum != xmitcsum)
	    putDebugChar ('-');	/* failed checksum */
	  else
	  {
	    putDebugChar ('+');	/* successful transfer */
	    /* if a sequence char is present, reply the sequence ID */
	    if (buffer[2] == ':')
		{
		  putDebugChar (buffer[0]);
		  putDebugChar (buffer[1]);
		  /* remove sequence chars from buffer */
		  count = strlen (buffer);
		  for (i = 3; i <= count; i++)
		    buffer[i - 3] = buffer[i];
		}
	  }
	}
  }
  while (checksum != xmitcsum);

}

/*-------------------------------------------------------------------------*/
static void
putpacket (register char *buffer)
{
  register  int checksum;
  register  int count;

  /*  $<packet info>#<checksum>. */
  do
  {
    char *src = buffer;
    putDebugChar ('$');
    checksum = 0;

    while (*src)
	{
	  int runlen;

	  /* Do run length encoding */
	  for (runlen = 0; runlen < 100; runlen ++) 
	  {
	    if (src[0] != src[runlen] || runlen == 99) 
		{
		  if (runlen > 3) 
		  {
		    int encode;
		    /* Got a useful amount */
		    putDebugChar (*src);
		    checksum += *src;
		    putDebugChar ('*');
		    checksum += '*';
		    checksum += (encode = runlen + ' ' - 4);
		    putDebugChar (encode);
		    src += runlen;
		  }
		  else
		  {
		    putDebugChar (*src);
		    checksum += *src;
		    src++;
		  }
		  break;
		}
	  }
	}


    putDebugChar ('#');
    putDebugChar (highhex(checksum));
    putDebugChar (lowhex(checksum));
  }
  while  (getDebugChar() != '+');

}

/*-------------------------------------------------------------------------*/
static char
isBranchConditionMet(unsigned char reason)
{
  char c, z, n, v, nv;
  char res = 0;
  
  c = ((unsigned char)registers[CCR] >> CCR_C) & 0x01;
  z = ((unsigned char)registers[CCR] >> CCR_Z) & 0x01;
  v = ((unsigned char)registers[CCR] >> CCR_V) & 0x01;
  n = ((unsigned char)registers[CCR] >> CCR_N) & 0x01;
  nv = n ^ v;

  switch (reason)
  {
    case 0:		/* BRA */
      res = 1;
      break;
    case 1:		/* BRN */
      break;
    case 2:		/* BHI */
      if ((c | z) == 0)
        res = 1;
      break;
    case 3:		/* BLS */
      if ((c | z) == 1)
        res = 1;
      break;
    case 4:		/* BCC */
      if (c == 0)
        res = 1;
      break;
    case 5:		/* BCS */
      if (c == 1)
        res = 1;
      break;
    case 6:		/* BNE */
      if (z == 0)
        res = 1;
      break;
    case 7:		/* BEQ */
      if (z == 1)
        res = 1;
      break;
    case 8:		/* BVC */
      if (v == 0)
        res = 1;
      break;
    case 9:		/* BVS */
      if (v == 1)
        res = 1;
      break;
    case 0xA:	/* BPL */
      if (n == 0)
        res = 1;
      break;
    case 0xB:	/* BMI */
      if (n == 1)
        res = 1;
      break;
    case 0xC:	/* BGE */
      if (nv == 0)
        res = 1;
      break;
    case 0xD:	/* BLT */
      if (nv == 1)
        res = 1;
      break;
    case 0xE:	/* BGT */
      if ((z | nv) == 0)
        res = 1;
      break;
    case 0xF:	/* BLE */
      if ((z | nv) == 1)
        res = 1;
      break;
  }

  return res;
}

/*-------------------------------------------------------------------------*/
static char
computeInstrLen(short *pInstr)
{
  char nRes = 1;
  short si;
  char ci1, ci2, ci3;

  si = *pInstr;
  ci1 = (si & 0xF000) >> 12;
  ci2 = (si & 0x0F00) >> 8;
  ci3 = (si & 0x00F0) >> 4;

  if (ci1==5)
  {
    if ((ci2 == 8) || (ci2 == 0x0A) || (ci2 == 0x0C) || (ci2 == 0x0E))
      nRes = 2;
  }
  else if (ci1 == 6)
  {
    if ((ci2 == 0xE) || (ci2 == 0xF))
      nRes = 2;
    if (ci2 == 0xA)
    {
      if ((ci3 == 0) || (ci3 == 4) || (ci3 == 8) || (ci3 == 0xC))
        nRes = 2;
      else if ((ci3 == 2) || (ci3 == 0xA))
        nRes = 3;
    }
    if (ci2 == 0xB)
    {
      if ((ci3 == 0) || (ci3 == 8))
        nRes = 2;
      else if ((ci3 == 2) || (ci3 == 0xA))
        nRes = 3;
    }
  }
  else if (ci1 == 7)
  {
    if (ci2 == 0xA)
      nRes = 3;
    else if (ci2 == 8)
      nRes = 4;
    else if (ci2 >= 9)
      nRes = 2;
  }
  else if (((si & 0xFF00) == 0x0100) && (si != 0x0180))
    nRes = computeInstrLen2((long *)pInstr);

  return nRes;
}

/*-------------------------------------------------------------------------*/
static char
computeInstrLen2(long *pInstr)
{
  char nRes = 2;
  long li, li1, li2;
  
  li = *pInstr;
  li1 = li & 0xFFFFFFF0;
  li2 = li & 0xFFFFFF00;

  if ((li2 == 0x01006F00) ||
    (li1 == 0x01006B00) ||
    (li1 == 0x01006B80))
    nRes = 3;
  else if ((li1 == 0x01006B20) ||
    (li1 == 0x01006BA0))
    nRes = 4;
  else if ((li2 == 0x01007800) ||
  	(li1 == 0x01407800))
    nRes = 5;
  return nRes;
}

/*-------------------------------------------------------------------------*/
static int
computeSignal (int exceptionVector)
{
  switch (exceptionVector)
  {
    case IntID_Reserved:
      return 4;       // SIGILL

    case IntID_NMI:
    case IntID_RXI0:
      return 2;       /* SIGINT */

    case IntID_TRAP0:
    case IntID_TRAP1:
    case IntID_TRAP2:
    case IntID_TRAP3:
      return 5;       /* SIGTRAP */

    default:
      return 7;       /* "software generated" */
  }
}

/*-------------------------------------------------------------------------*/
static void
doSStep (void)
{
  short *instrMem;
  char instrLen;
  int displacement;
  unsigned char reg;
  unsigned short opcode;
  unsigned char reason;
  unsigned char op1, op2;
  unsigned long ulOp, ulAddr;

  instrMem = (short *)registers[PC];
  stepped = 1;
  opcode = *instrMem;
  op1 = (opcode & 0xFF00) >> 8;
  op2 = opcode & 0x00FF;
  
  switch (op1)
  {
  	case OP_BRA_D8:		/* Bcc d:8 */
	case OP_BRN_D8:
	case OP_BHI_D8:
	case OP_BLS_D8:
	case OP_BCC_D8:
	case OP_BCS_D8:
	case OP_BNE_D8:
	case OP_BEQ_D8:
	case OP_BVC_D8:
	case OP_BVS_D8:
	case OP_BPL_D8:
	case OP_BMI_D8:
	case OP_BGE_D8:
	case OP_BLT_D8:
	case OP_BGT_D8:
	case OP_BLE_D8:
      reason = op1 & BCC_D8_OP1_MASK;
      if (isBranchConditionMet(reason))
      {
        displacement = (signed char)op2;
        instrMem = (short *)(registers[PC] + displacement + 2);
      }
      else
      {
        instrLen = computeInstrLen(instrMem);
        instrMem += instrLen;
      }
      break;
	  
    case OP_RTS:		/* RTS */
	  if (op2 == OP_RTS_2)
	  {
		instrMem = (short *) (*((unsigned long *)(registers[R7] & ADDR_24)) & ADDR_24);
	  }
	  break;

    case OP_BSR_D8:		/* BSR d:8 */
      displacement = (signed char)op2;
      instrMem = (short *)(registers[PC] + displacement + 2);
      break;

    case OP_RTE:		/* RTE */
	  if (op2 == OP_RTE_2)
	  {
        registers[CCR] = (*((unsigned long *)(registers[R7] & ADDR_24)) & 0xFF000000) >> 24;
        instrMem = (short *) (*((unsigned long *)(registers[R7] & ADDR_24)) & ADDR_24);
	  }
	  break;

	case OP_BCC_D16:	/* Bcc d:16 */
      reason = (op2 & BCC_D16_OP2_MASK) >> 4;
      ulOp = *((unsigned long *)instrMem);
      if (isBranchConditionMet(reason))
      {
        displacement = (signed short)(ulOp & 0xFFFF);
        instrMem = (short *)(registers[PC] + displacement + 4);
      }
      else
      {
        instrLen = computeInstrLen(instrMem);
        instrMem += instrLen;
      }
	  break;

	case OP_JMP_ER:		/* JMP @ERn */
    case OP_JSR_ER:		/* JSR @ERn */
      reg = (op2 & JMP_ER_OP2_MASK) >> 4;
      instrMem = (short *) (registers[reg] & ADDR_24);
	  break;

	case OP_JMP_AA24:	/* JMP @aa:24 */
    case OP_JSR_AA24:	/* JSR @aa:24 */
      ulOp = *((unsigned long *)instrMem);
	  ulAddr = ulOp & ADDR_24;
	  instrMem = (short *)ulAddr;
	  break;

	case OP_JMP_AA8:	/* JMP @@aa:8 */
    case OP_JSR_AA8:	/* JSR @@aa:8 */
      displacement = op1;
      instrMem = (short*)(*((unsigned long*)(displacement)) & ADDR_24);
	  break;

	case OP_BSR_D16:	/* BSR d:16 */
      if (op2 == OP_BSR_D16_2)
      {
        ulOp = *((unsigned long *)instrMem);
        displacement = (signed short)(ulOp & 0xFFFF);
        instrMem = (short *)(registers[PC] + displacement + 4);
      }
	  break;

	default:
      instrLen = computeInstrLen(instrMem);
      instrMem += instrLen;
	  break;
  }

  instrBuffer.memAddr = instrMem;
  instrBuffer.oldInstr = *instrMem;
  *instrMem = SSTEP_INSTR;
}

/*-------------------------------------------------------------------------*/
static void
undoSStep (void)
{
  if (stepped)
  {
    short *instrMem;
    instrMem = instrBuffer.memAddr;
    *instrMem = instrBuffer.oldInstr;
  }
  stepped = 0;
}

/*-------------------------------------------------------------------------*/
static void
gdb_handle_exception (int exceptionVector)
{
  int sigval;
  int addr, length;
  int regno;
  char *ptr;

  /* reply to host that an exception has occurred */
  sigval = computeSignal (exceptionVector);
  remcomOutBuffer[0] = 'S';
  remcomOutBuffer[1] = highhex(sigval);
  remcomOutBuffer[2] = lowhex (sigval);
  remcomOutBuffer[3] = 0;

  putpacket (remcomOutBuffer);

  /*
   * TRAP_VEC exception indicates a software trap
   * inserted in place of code ... so back up
   * PC by one instruction, since this instruction
   * will later be replaced by its original one!
   */
  if ((exceptionVector == IntID_TRAP1) ||
    (exceptionVector == IntID_TRAP2) ||
	(exceptionVector == IntID_TRAP3))
    registers[PC] -= 2;

  /*
   * Do the thangs needed to undo
   * any stepping we may have done!
   */
  undoSStep ();

  while (1)
  {
    remcomOutBuffer[0] = 0;
    getpacket (remcomInBuffer);

    switch (remcomInBuffer[0])
	{
	case '?':
	  remcomOutBuffer[0] = 'S';
	  remcomOutBuffer[1] = highhex (sigval);
	  remcomOutBuffer[2] = lowhex (sigval);
	  remcomOutBuffer[3] = 0;
	  break;
	case 'd':
	  remote_debug = !(remote_debug);	/* toggle debug flag */
	  break;
	case 'g':		/* return the value of the CPU registers */
	  mem2hex ((char *) registers, remcomOutBuffer, NUMREGBYTES);
	  break;
	case 'G':		/* set the value of the CPU registers - return OK */
	  hex2mem (&remcomInBuffer[1], (char *) registers, NUMREGBYTES);
	  strcpy (remcomOutBuffer, "OK");
	  break;

	  /* mAA..AA,LLLL  Read LLLL bytes at address AA..AA */
	case 'm':
	  /* TRY, TO READ %x,%x.  IF SUCCEED, SET PTR = 0 */
	  ptr = &remcomInBuffer[1];
	  if (hex2int (&ptr, &addr))
		if (*(ptr++) == ',')
		  if (hex2int (&ptr, &length))
		  {
		    ptr = 0;
		    mem2hex ((char *) addr, remcomOutBuffer, length);
		  }
	  if (ptr)
		strcpy (remcomOutBuffer, "E01");
	  break;

	  /* MAA..AA,LLLL: Write LLLL bytes (encoded hex) at address AA.AA return OK */
	  /* XAA..AA,LLLL: Write LLLL bytes (encoded escaped-binary) at address AA.AA return OK */
	case 'M':
	case 'X':
	  /* TRY, TO READ '%x,%x:'.  IF SUCCEED, SET PTR = 0 */
	  ptr = &remcomInBuffer[1];
	  if (hex2int (&ptr, &addr))
		if (*(ptr++) == ',')
		  if (hex2int (&ptr, &length))
		    if (*(ptr++) == ':')
		    {
		      if (remcomInBuffer[0] == 'M')
			    hex2mem (ptr, (char *) addr, length);
			  else
			    ebin2mem (ptr, (char *) addr, length);
			  ptr = 0;
			  strcpy (remcomOutBuffer, "OK");
		    }
	  if (ptr)
		strcpy (remcomOutBuffer, "E02");
	  break;
	  
	case 'P' : /* set the value of a single CPU register - return OK */
	  ptr = &remcomInBuffer[1];
	  if (hex2int(&ptr, &regno) && *ptr++ == '=')
	    if (regno >= 0 && regno < NUMREGS)
	    {
		  hex2mem (ptr, (char *)&registers[regno], 4);
		  strcpy(remcomOutBuffer,"OK");
		  break;
	    }
	  strcpy (remcomOutBuffer, "E01");
	  break;

	  /* cAA..AA    Continue at address AA..AA(optional) */
	  /* sAA..AA   Step one instruction from AA..AA(optional) */
	case 'c':
	case 's':
	  /* TRY, to read optional parameter, pc unchanged if no parm */
	  ptr = &remcomInBuffer[1];
	  if (hex2int (&ptr, &addr))
	    registers[PC] = addr & ADDR_24;

	  if (remcomInBuffer[0] == 's')
	    doSStep ();
	  return;
	  break;

	  /* kill the program */
	case 'k':		/* do nothing */
	  break;
	}			/* switch */

    /* reply to the request */
    putpacket (remcomOutBuffer);
  }
}

/*-------------------------------------------------------------------------*/
void
handle_exception(int exceptionVector)
{
  if(exceptionVector ==   IntID_TRAP1)
  {
    handle_bios_call ();
    return;
  }
     
  ingdbmode = 1;
  gdb_handle_exception (exceptionVector);
}

/*-------------------------------------------------------------------------*/
void
breakpoint (void)
{
  asm volatile("trapa	#0");
}

/*-------------------------------------------------------------------------*/
void
start_gdbstub (void)
{
  in_nmi = 0;
  stepped = 0;

  stub_sp = stub_stack + stub_stack_size;
  breakpoint ();

  while (1)
    ;
}

/*-------------------------------------------------------------------------*/
static void
handle_bios_call (void)
{
  unsigned int func = registers[R0];
  unsigned int arg0 = registers[R1];
  unsigned int arg1 = registers[R2];
  unsigned int arg2 = registers[R3];
  unsigned int arg3 = registers[R4];
  unsigned int ret;

  switch (func)
  {
    case 0:
      if (ingdbmode)
      {
        remcomOutBuffer[0] = 'O';
        remcomOutBuffer[1] = highhex(arg0);
        remcomOutBuffer[2] = lowhex (arg0);
        remcomOutBuffer[3] = 0;
        putpacket (remcomOutBuffer);
      }
      else
      {
        int ch;
        char *p = (char *)arg0;
            
        while ((ch = *p++))
          putDebugChar (ch);
      }
      ret = 0;
      break;

    case 1:
      /* Second strage access */
      /* Not implemented yet */
      ret = ~0;
      break;
    case 255:
      /* Detach gdb mode */
      ingdbmode = 0;
      putpacket ("W00");
      getDebugChar ();
      ret = 0;
      break;

    default:
      /* Do nothing */
      ret = ~0;
      break;
  }

  registers[R0] = ret;
}
