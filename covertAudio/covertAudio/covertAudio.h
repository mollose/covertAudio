#include <ntddk.h>
#include <intrin.h>
#include <math.h>

#define DBG_TRACE(src, msg) DbgPrint("[%s] : %s\n", src, msg)
#define DBG_PRINT1(arg) DbgPrint("%s\n", arg1)
#define DBG_PRINT2(fmt, arg1) DbgPrint(fmt, arg1)
#define DBG_PRINT3(fmt, arg1, arg2) DbgPrint(fmt, arg1, arg2)

#define IOCTL_INIT_RETASK \
	CTL_CODE(FILE_DEVICE_SOUND, 0x801, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_PREPARE_RETASK \
	CTL_CODE(FILE_DEVICE_SOUND, 0x802, METHOD_BUFFERED, FILE_READ_DATA|FILE_WRITE_DATA)
#define IOCTL_DO_RETASK \
	CTL_CODE(FILE_DEVICE_SOUND, 0x803, METHOD_BUFFERED, FILE_WRITE_DATA)

#define UWORD_MAX 65536
#define HDA_REGISTER_SIZE 0x80
#define HDA_STREAMDESC_SIZE 0x20
#define CODEC_DELAY 1000
#define MAX_WAITUP 500
#define MAX_RIRBWAIT 1000
#define MAX_WIDGET 256
#define MAX_CODEC 16
#define BUF_SIZE /*64 * 1024 * 4*/ 1024 * 2
#define BLOCK_NUM 2 //256
#define BLOCK_SIZE BUF_SIZE / BLOCK_NUM

#define csr32(m, r) (*(ulong*)&(m)[r])
#define csr16(m, r) (*(ushort*)&(m)[r])
#define csr8(m, r) (*(uchar*)&(m)[r])

typedef unsigned char uchar;
typedef unsigned long ulong;
typedef unsigned short ushort;
typedef unsigned int uint;

typedef struct widget Widget;
typedef struct funcgroup Funcgroup;

struct Data8
{
	uchar type[3];
};

struct Datab
{
	bool type[3];
};

union ConfigData
{
	struct
	{
		uchar fourth;
		uchar third;
		uchar second;
		uchar first;
	};
	uint all;
};

struct JackInfo
{
	ulong nid, info;
};

struct Id
{
	uint codec, nid;
};

struct Amp
{
	Datab leftMute;
	Data8 leftGain;
	Datab rightMute;
	Data8 rightGain;
};

struct Codec
{
	Id id;
	uint vid, rid;
	Widget* widgets[MAX_WIDGET];
	Funcgroup* fgroup;
};

struct widget
{
	Id id;
	Funcgroup* fg;
	uint type;
	bool isConnList;
	bool isOutAmp;
	bool isInAmp;
	bool isStereo;

	uint nlist;
	Widget** list;

	Amp outAmp;
	Amp* inAmp;

	union
	{
		struct
		{
			uchar portConn;
			uchar upperLoc;
			uchar lowerLoc;
			Data8 defaultDev;
			uchar connType;
			uchar color;

			bool isInCap;
			bool isOutCap;
			bool isDetectCap;
			Datab isOut;
			Datab isIn;
			bool isDetect;
		};
		struct
		{
			Data8 strm;
			Data8 lowChan;
		};
	};

	Widget* out;
	Widget* in;
	Widget* end;
};

struct funcgroup
{
	Id id;
	Codec* codec;
	uint type;
	uint base;
	uint nWidgets;
	uint nWains;
	uint nWaouts;
	uint nPins;
	Funcgroup* next;
};

struct Stream
{
	uint strm;
	uint offset;

	bool run;
	
	uint base, mult, div, bits, chan;
};

struct JackRetask
{
	Widget* jack;
	Widget *aout, **ain;
	Stream *ostrm, **istrm;
	uint nain, nistrm, vol;

	ulong oformer, olatter;
	ulong iformer, ilatter;
};

struct myAudio
{
	uchar* mem;
	ulong size;

	uchar* strm;
	ulong iss;
	ulong oss;
	ulong bss;

	ulong* corb;
	ulong corbsize;

	ulong* rirb;
	ulong rirbsize;

	Stream* istream;
	Stream* ostream;
	Stream* bstream;
	Codec codec;
};

enum
{
	oldData = 0,
	inData = 1,
	outData = 2,
};

enum {
	Gcap = 0x00,
	Oss = 0xf << 12,
	Iss = 0xf << 8,
	Bss = 0xf << 4,
	Gctl = 0x08,
	Rst = 1,
	Flush = 2,
	Acc = 1 << 8,
	Wakeen = 0x0c,
	Statests = 0x0e,
	Sdiwake = 1 | 2 | 4,
	Intctl = 0x20,
	Gie = 1 << 31,
	Cie = 1 << 30,
	Intsts = 0x24,
	Gis = 1 << 31,
	Cis = 1 << 30,
	Sismask = 0xff,
	Walclk = 0x30,
	Corblbase = 0x40,
	Corbubase = 0x44,
	Corbwp = 0x48,
	Corbrp = 0x4a,
	Corbptrrst = 1 << 15,
	Corbctl = 0x4c,
	Corbdma = 2,
	Corbint = 1,
	Corbsts = 0x4d,
	Cmei = 1,
	Corbsz = 0x4e,
	Rirblbase = 0x50,
	Rirbubase = 0x54,
	Rirbwp = 0x58,
	Rirbptrrst = 1 << 15,
	Rintcnt = 0x5a,
	Rirbctl = 0x5c,
	Rirbover = 4,
	Rirbdma = 2,
	Rirbint = 1,
	Rirbsts = 0x5d,
	Rirbrover = 4,
	Rirbrint = 1,
	Rirbsz = 0x5e,
	Immcmd = 0x60,
	Immresp = 0x64,
	Immstat = 0x68,
	Dplbase = 0x70,
	Dpubase = 0x74,
	Sdctl = 0x00,
	Srst = 1 << 0,
	Srun = 1 << 1,
	Scie = 1 << 2,
	Seie = 1 << 3,
	Sdie = 1 << 4,
	Stagbit = 20,
	Stagmask = 0xf00000,
	Sdsts = 0x03,
	Scompl = 1 << 2,
	Sfifoerr = 1 << 3,
	Sdescerr = 1 << 4,
	Sfifordy = 1 << 5,
	Sdlpib = 0x04,
	Sdcbl = 0x08,
	Sdlvi = 0x0c,
	Sdfifow = 0x0e,
	Sdfifos = 0x10,
	Sdfmt = 0x12,
	Fmtchan = 0xf,
	Fmtbits = 3 << 4,
	Fmtdiv = 7 << 8,
	Fmtmul = 7 << 11,
	Fmtbase = 1 << 14,
	Sdbdplo = 0x18,
	Sdbdphi = 0x1c,
};

enum {
	Getparm = 0xf00,
	Vendorid = 0x00,
	Revid = 0x02,
	Subnodecnt = 0x04,
	Fungrtype = 0x05,
	Graudio = 0x01,
	Grmodem = 0x02,
	Fungrcap = 0x08,
	Widgetcap = 0x09,
	Waout = 0,
	Wain = 1,
	Wamix = 2,
	Wasel = 3,
	Wpin = 4,
	Wpower = 5,
	Wknob = 6,
	Wbeep = 7,
	Winampcap = 0x0002,
	Woutampcap = 0x0004,
	Wampovrcap = 0x0008,
	Wfmtovrcap = 0x0010,
	Wstripecap = 0x0020,
	Wproccap = 0x0040,
	Wunsolcap = 0x0080,
	Wconncap = 0x0100,
	Wdigicap = 0x0200,
	Wpwrcap = 0x0400,
	Wlrcap = 0x0800,
	Wcpcap = 0x1000,
	Streamrate = 0x0a,
	Streamfmt = 0x0b,
	Pincap = 0x0c,
	Psense = 1 << 0,
	Ptrigreq = 1 << 1,
	Pdetect = 1 << 2,
	Pheadphone = 1 << 3,
	Pout = 1 << 4,
	Pin = 1 << 5,
	Pbalanced = 1 << 6,
	Phdmi = 1 << 7,
	Inampcap = 0x0d,
	Outampcap = 0x12,
	Connlistlen = 0x0e,
	Powerstates = 0x0f,
	Processcap = 0x10,
	Gpiocount = 0x11,
	Knobcap = 0x13,
	Getconn = 0xf01,
	Setconn = 0x701,
	Getconnlist = 0xf02,
	Getstate = 0xf03,
	Setstate = 0x703,
	Getstream = 0xf06,
	Setstream = 0x706,
	Getpinctl = 0xf07,
	Setpinctl = 0x707,
	Pinctlin = 1 << 5,
	Pinctlout = 1 << 6,
	Getunsolresp = 0xf08,
	Setunsolresp = 0x708,
	Getpinsense = 0xf09,
	Exepinsense = 0x709,
	Getgpi = 0xf10,
	Setgpi = 0x710,
	Getbeep = 0xf0a,
	Setbeep = 0x70a,
	Getknob = 0xf0f,
	Setknob = 0x70f,
	Setdefault = 0x71e,
	Getdefault = 0xf1c,
	Funreset = 0x7ff,
	Getchancnt = 0xf2d,
	Setchancnt = 0x72d,
	Getcoef = 0xd,
	Setcoef = 0x5,
	Getproccoef = 0xc,
	Setproccoef = 0x4,
	Getamp = 0xb,
	Setamp = 0x3,
	Asetout = 1 << 15,
	Asetin = 1 << 14,
	Asetleft = 1 << 13,
	Asetright = 1 << 12,
	Asetmute = 1 << 7,
	Aidx = 8,
	Again = 0,
	Getconvfmt = 0xa,
	Setconvfmt = 0x2,
};

extern myAudio* myaud;

void lockRoutine(PKDPC, PVOID, PVOID, PVOID);
NTSTATUS ReleaseLock(PVOID);
PKDPC AcquireLock();
KIRQL RaiseIRQL();
void LowerIRQL(KIRQL);

int enumDevice();
uint Cmd(Id id, uint verb, uint param);
int initRetask(Widget* startp, Widget* endp, int datatype, int vol);
void doRetask(Widget* w, int datatype, int strm, int lowChan);
void restoreRetask(Widget* w, int datatype);
void getStream();
void getWidget(Widget* w, int datatype);