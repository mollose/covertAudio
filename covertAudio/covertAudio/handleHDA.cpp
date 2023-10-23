#include "covertAudio.h"

static char widgetType[][10] = {"Output", "Input", "Mixer", "Selector", "Pin", "Power", "Volume", "Beep"};
static char portConn[][10] = { "Jack", "NoConn", "FixedFunc", "Both" };
static char upperLoc[][10] = { "External", "Internal", "Seperate", "Other" };
static char lowerLoc[][10] = { "N/A", "Rear", "Front", "Left", "Right", "Top", "Bottom" };
static char defaultDev[][10] = { "LineOut", "Speaker", "HPOut", "CD", "SPDIFOut",
								 "DigiOut", "ModemLine", "Handset", "LineIn", "AUX", "MicIn", "Telephony",
								 "SPDIFIn", "DigiIn", "Reserved", "Other" };
static char connType[][10] = { "Unknown", "1/8\"S/M", "1/4\"S/M", "ATAPI", "RCA", "Optical", "DigiOther",
							   "AnlgOther", "DIN", "XLR/Pro", "Modem", "Combi", "Other" };
static char jackColor[][10] = { "Unknown", "Black", "Grey", "Blue", "Green", "Red", "Orange", "Yellow",
"Purple", "Pink", "Reserved", "White", "Other" };

void microdelay(uint microsec)
{
	static LARGE_INTEGER beginTime;
	static LARGE_INTEGER endTime;
	static LARGE_INTEGER frequency;
	beginTime = KeQueryPerformanceCounter(&frequency);

	while (1)
	{
		endTime = KeQueryPerformanceCounter(NULL);
		if (((endTime.QuadPart - beginTime.QuadPart) * 1000000)
			/ frequency.QuadPart >= microsec)
			break;
	}
}

static int sendCmd(Id id, uint verb, uint param, uint *ret)
{
	uint request, reply[2];
	uint rp, wp, re;

	request = (id.codec << 28) | (id.nid << 20);
	if ((verb & 0x700) == 0x700)
		request |= (verb << 8) | param;
	else
		request |= (verb << 16) | param;

	re = csr16((*myaud).mem, Rirbwp);
	rp = csr16((*myaud).mem, Corbrp);
	wp = (csr16((*myaud).mem, Corbwp) + 1) % (*myaud).corbsize;

	if (rp == wp)
	{
		DBG_TRACE("sendCmd", "CORB full");
		return -1;
	}

	(*myaud).corb[wp] = request;
	csr16((*myaud).mem, Corbwp) = wp;

	for (uint i = 0; i < MAX_RIRBWAIT; i++)
	{
		if (csr16((*myaud).mem, Rirbwp) != re)
		{
			re = (re + 1) % (*myaud).rirbsize;
			memcpy(reply, &(*myaud).rirb[re * 2], 8);
			*ret = reply[0];

			//DbgPrint("[sendCmd] : CodecID %X, NID %X, Verb %X, Param %X, Reply %X\n", 
				//id.codec, id.nid, verb, param, reply[0]);

			return 0;
		}
		microdelay(1);
	}

	return -1;
}

uint Cmd(Id id, uint verb, uint param)
{
	uint reply[2];
	if (sendCmd(id, verb, param, reply) == -1)
		return 0;

	return reply[0];
}

static void setAmp(Widget* w, bool inout, int datatype)
{
	ushort amp;

	if (inout == 0)
	{
		amp = 0xa << 12;
		amp |= (w->outAmp.leftMute.type[datatype] << 7);
		amp |= w->outAmp.leftGain.type[datatype];
		Cmd(w->id, Setamp, amp);
		amp = 0x9 << 12;
		amp |= (w->outAmp.rightMute.type[datatype] << 7);
		amp |= w->outAmp.rightGain.type[datatype];
		Cmd(w->id, Setamp, amp);
	}
	else if (inout == 1)
	{
		for (uint i = 0; i < w->nlist; i++)
		{
			amp = 0x6 << 12;
			amp |= i << 8;
			amp |= (w->inAmp[i].leftMute.type[datatype] << 7);
			amp |= w->inAmp[i].leftGain.type[datatype];
			Cmd(w->id, Setamp, amp);
			amp = 0x5 << 12;
			amp |= i << 8;
			amp |= (w->inAmp[i].rightMute.type[datatype] << 7);
			amp |= w->inAmp[i].leftGain.type[datatype];
			Cmd(w->id, Setamp, amp);
		}
	}
}

static void getAmp(Widget* w, bool inout, int datatype)
{
	uint amp;

	if (inout == 0)
	{
		amp = Cmd(w->id, Getamp, 5 << 13);
		w->outAmp.leftMute.type[datatype] = amp >> 7;
		w->outAmp.leftGain.type[datatype] = amp & 0x7f;
		amp = Cmd(w->id, Getamp, 4 << 13);
		w->outAmp.rightMute.type[datatype] = amp >> 7;
		w->outAmp.rightGain.type[datatype] = amp & 0x7f;

		DbgPrint("[OutAmp] : Left %s %X, Right %s %X\n", 
			w->outAmp.leftMute.type[datatype] ? "M" : "E", w->outAmp.leftGain.type[datatype],
			w->outAmp.rightMute.type[datatype] ? "M" : "E", w->outAmp.rightGain.type[datatype]);
	}
	else if (inout == 1)
	{
		for (uint i = 0; i < w->nlist; i++)
		{
			amp = Cmd(w->id, Getamp, (1 << 13) | i);
			w->inAmp[i].leftMute.type[datatype] = amp >> 7;
			w->inAmp[i].leftGain.type[datatype] = amp & 0x7f;
			amp = Cmd(w->id, Getamp, i);
			w->inAmp[i].rightMute.type[datatype] = amp >> 7;
			w->inAmp[i].rightGain.type[datatype] = amp & 0x7f;

			DbgPrint("[InAmp(Connect #%X)] : Left %s %X, Right %s %X\n", i,
				w->inAmp[i].leftMute.type[datatype] ? "M" : "E", w->inAmp[i].leftGain.type[datatype],
				w->inAmp[i].rightMute.type[datatype] ? "M" : "E", w->inAmp[i].rightGain.type[datatype]);
		}
	}
}

void getStream()
{
	Stream* cur;
	uint i, offset;

	DbgPrint("=====================INPUT STREAM DESCRIPTOR======================\n");
	for (i = 0; i < (*myaud).iss; i++)
	{
		offset = HDA_STREAMDESC_SIZE * i;
		cur = &(*myaud).istream[i];
		cur->strm = (csr32((*myaud).strm, offset + Sdctl) & Stagmask) >> 20;
		cur->offset = offset;
		cur->run = (csr32((*myaud).strm, offset + Sdctl) & Srun) >> 1;
		cur->base = (csr16((*myaud).strm, offset + Sdfmt) & Fmtbase) >> 14;
		cur->mult = (csr16((*myaud).strm, offset + Sdfmt) & Fmtmul) >> 11;
		cur->div = (csr16((*myaud).strm, offset + Sdfmt) & Fmtdiv) >> 8;
		cur->bits = (csr16((*myaud).strm, offset + Sdfmt) & Fmtbits) >> 4;
		cur->chan = csr16((*myaud).strm, offset + Sdfmt) & Fmtchan;
		DbgPrint("[searchHDA] : IStream #%X, Run %X, Base %s, Mult %X, Div %X, Bits %X, Chan %X\n",
			cur->strm, cur->run, cur->base ? "44.1kHz" : "48kHz", cur->mult, cur->div, cur->bits, cur->chan);
	}

	DbgPrint("=====================OUTPUT STREAM DESCRIPTOR====================\n");
	for (i = 0; i < (*myaud).oss; i++)
	{
		offset = (HDA_STREAMDESC_SIZE * (*myaud).iss) + (HDA_STREAMDESC_SIZE * i);
		cur = &(*myaud).ostream[i];
		cur->strm = (csr32((*myaud).strm, offset + Sdctl) & Stagmask) >> 20;
		cur->offset = offset;
		cur->run = (csr32((*myaud).strm, offset + Sdctl) & Srun) >> 1;
		cur->base = (csr16((*myaud).strm, offset + Sdfmt) & Fmtbase) >> 14;
		cur->mult = (csr16((*myaud).strm, offset + Sdfmt) & Fmtmul) >> 11;
		cur->div = (csr16((*myaud).strm, offset + Sdfmt) & Fmtdiv) >> 8;
		cur->bits = (csr16((*myaud).strm, offset + Sdfmt) & Fmtbits) >> 4;
		cur->chan = csr16((*myaud).strm, offset + Sdfmt) & Fmtchan;

		DbgPrint("[searchHDA] : OStream #%X, Run %X, Base %s, Mult %X, Div %X, Bits %X, Chan %X\n",
			cur->strm, cur->run, cur->base ? "44.1kHz" : "48kHz", cur->mult, cur->div, cur->bits, cur->chan);
	}

	DbgPrint("==================BIDIRECTIONAL STREAM DESCRIPTOR=================\n");
	for (i = 0; i < (*myaud).bss; i++)
	{
		offset = (HDA_STREAMDESC_SIZE * (*myaud).iss)
			+ (HDA_STREAMDESC_SIZE * (*myaud).oss) + (HDA_STREAMDESC_SIZE * i);
		cur = &(*myaud).bstream[i];
		cur->strm = (csr32((*myaud).strm, offset + Sdctl) & Stagmask) >> 20;
		cur->offset = offset;
		cur->run = (csr32((*myaud).strm, offset + Sdctl) & Srun) >> 1;
		cur->base = (csr16((*myaud).strm, offset + Sdfmt) & Fmtbase) >> 14;
		cur->mult = (csr16((*myaud).strm, offset + Sdfmt) & Fmtmul) >> 11;
		cur->div = (csr16((*myaud).strm, offset + Sdfmt) & Fmtdiv) >> 8;
		cur->bits = (csr16((*myaud).strm, offset + Sdfmt) & Fmtbits) >> 4;
		cur->chan = csr16((*myaud).strm, offset + Sdfmt) & Fmtchan;
		DbgPrint("[searchHDA] : BStream #%X, Run %X, Base %s, Mult %X, Div %X, Bits %X, Chan %X\n",
			cur->strm, cur->run, cur->base ? "44.1kHz" : "48kHz", cur->mult, cur->div, cur->bits, cur->chan);
	}

	DbgPrint("=====================================================================\n");
}

static void setWidget(Widget* w, int datatype)
{
	uchar strm = 0, pinconf =0, pinctl = 0;

	if (w->isOutAmp || w->isInAmp)
	{
		if (w->isOutAmp) setAmp(w, 0, datatype);
		if (w->isInAmp) setAmp(w, 1, datatype);
	}

	switch (w->type)
	{
	case Waout:
	case Wain:
	{
		strm |= (w->strm.type[datatype] << 4);
		strm |= w->lowChan.type[datatype];
		Cmd(w->id, Setstream, strm);
	}
	break;
	case Wpin:
	{
		pinconf |= (w->defaultDev.type[datatype] << 4);
		pinconf |= w->connType;
		Cmd(w->id, Setdefault, pinconf);

		pinctl |= (w->isOut.type[datatype] << 6);
		pinctl |= (w->isIn.type[datatype] << 5);
		Cmd(w->id, Setpinctl, pinctl);
	}
	break;
	}
}

void getWidget(Widget* w, int datatype)
{	
	uint strm, pinconf, pincap, pinctl;

	if (w->isOutAmp || w->isInAmp)
	{
		DbgPrint("------------------AMP------------------\n");

		if (w->isOutAmp) getAmp(w, 0, datatype);
		if (w->isInAmp) getAmp(w, 1, datatype);

		DbgPrint("----------------------------------------\n");
	}

	switch (w->type)
	{
	case Waout:
	{
		DbgPrint("------------------AUDIO OUT------------------\n");

		strm = Cmd(w->id, Getstream, 0);
		w->strm.type[datatype] = strm >> 4;
		w->lowChan.type[datatype] = strm & 0xf;

		DbgPrint("[Stream, Channel] : Stream %X, Lowest channel %X\n", 
			w->strm.type[datatype], w->lowChan.type[datatype]);

		if (datatype == oldData)
			w->fg->nWaouts++;
		DbgPrint("----------------------------------------------\n");
	}
	break;
	case Wain:
	{
		DbgPrint("-------------------AUDIO IN-------------------\n");

		strm = Cmd(w->id, Getstream, 0);
		w->strm.type[datatype] = strm >> 4;
		w->lowChan.type[datatype] = strm & 0xf;

		DbgPrint("[Stream, Channel] : Stream %X, Lowest channel %X\n",
			w->strm.type[datatype], w->lowChan.type[datatype]);

		if (datatype == oldData)
			w->fg->nWains++;
		DbgPrint("----------------------------------------------\n");
	}
	break;
	case Wpin:
	{
		DbgPrint("----------------------PIN COMPLEX----------------------\n");

		pinconf = Cmd(w->id, Getdefault, 0);
		w->portConn = pinconf >> 30;
		w->upperLoc = (pinconf >> 28) & 3;
		w->lowerLoc = (pinconf >> 24) & 0xf;
		w->defaultDev.type[datatype] = (pinconf >> 20) & 0xf;
		w->connType = (pinconf >> 16) & 0xf;
		w->color = (pinconf >> 12) & 0xf;

		DbgPrint("[Config] : PortConn %s, Loc %s %s\n",
			portConn[w->portConn], upperLoc[w->upperLoc], lowerLoc[w->lowerLoc]);
		DbgPrint("[Config] : DefaultDev %s, ConnType %s, Color %s\n",
			defaultDev[w->defaultDev.type[datatype]], connType[w->connType], jackColor[w->color]);

		pincap = Cmd(w->id, Getparm, Pincap);
		w->isInCap = (pincap >> 5) & 1;
		w->isOutCap = (pincap >> 4) & 1;
		w->isDetectCap = (pincap >> 2) & 1;

		DbgPrint("[Capablity] : Input %s, Output %s, Detect %s\n",
			w->isInCap ? "Y" : "N", w->isOutCap ? "Y" : "N", w->isDetectCap ? "Y" : "N");

		pinctl = Cmd(w->id, Getpinctl, 0);
		w->isOut.type[datatype] = (pinctl >> 6) & 1;
		w->isIn.type[datatype] = (pinctl >> 5) & 1;
		
		DbgPrint("[Control] : Input %s, Output %s\n",
			w->isIn.type[datatype] ? "enabled" : "disabled",
			w->isOut.type[datatype] ? "enabled" : "disabled");

		w->isDetect = Cmd(w->id, Getpinsense, 0) >> 31;
		DbgPrint("[Detect] Pin is %s\n", w->isDetect ? "detected" : "not detected");

		if (datatype == oldData)
			w->fg->nPins++;
		DbgPrint("---------------------------------------------------------\n");
	}
	break;
	}
}

Widget* srchWidget(Widget* curp, Widget* endp, int type, int orgtype)
{
	uint i;
	Widget *next, *result;

	DbgPrint("[srchWidget] : Widget #%X enter\n", curp->id.nid);
	
	if ((endp != NULL && curp == endp) || (endp == NULL && curp->type == type))
	{
		DbgPrint("[srchWidget] : Widget #%X found\n", curp->id.nid);
		return curp;
	}

	for (i = 0; i < curp->nlist; i++)
	{
		if (curp->isConnList)
		{
			next = curp->list[i];
			if (next->type == orgtype)
				continue;
		}
		else
		{
			DbgPrint("[srchWidget] : Widget #%X exit\n", curp->id.nid);
			return NULL;
		}

		result = srchWidget(next, endp, type, orgtype);
		if (result != NULL)
		{
			if (orgtype == Wpin)
			{
				curp->out = next;
				DbgPrint("[srchWidget] : Widget #%X -(out)-> Widget #%X\n", 
					curp->id.nid, curp->out->id.nid);
			}
			else if (orgtype == Wain)
			{
				curp->in = next;
				DbgPrint("[srchWidget] : Widget #%X -(in)-> Widget #%X\n",
					curp->id.nid, curp->in->id.nid);
			}
			else
				return NULL;

			return result;
		}
	}
	DbgPrint("[srchWidget] : Widget #%X exit\n", curp->id.nid);
	return NULL;
}

void restoreRetask(Widget* w, int datatype)
{
	Widget* cur = w;

	while (cur != NULL)
	{
		setWidget(cur, oldData);

		if (datatype == outData) cur = cur->out;
		else if (datatype == inData) cur = cur->in;
	}
}

void doRetask(Widget* w, int datatype, int strm, int lowChan)
{
	Widget *conv = NULL, *cur;
	cur = w;

	if (datatype == outData) conv = w->end;
	else if (datatype == inData) conv = w;

	conv->strm.type[datatype] = strm;
	conv->lowChan.type[datatype] = lowChan;

	while (cur != NULL)
	{
		setWidget(cur, datatype);
		getWidget(cur, datatype);

		if (datatype == outData) cur = cur->out;
		else if (datatype == inData) cur = cur->in;
	}
}

int initRetask(Widget* startp, Widget* endp, int datatype, int vol)
{
	Widget *result, *cur;
	cur = startp;

	if (datatype == outData)
	{
		result = srchWidget(startp, NULL, Waout, startp->type);
		if (result == NULL)
		{
			DBG_TRACE("initRetask", "Can't find retask path");
			return -1;
		}
		startp->end = result;
		DbgPrint("[initRetask] : Retask path to Widget #%X was found", result->id.nid);
		
		while (cur != NULL)
		{
			if (cur->isOutAmp)
			{
				cur->outAmp.leftMute.type[datatype] = 0;
				cur->outAmp.leftGain.type[datatype] = vol;
				cur->outAmp.rightMute.type[datatype] = 0;
				cur->outAmp.rightGain.type[datatype] = vol;
			}
			if (cur->isInAmp)
			{
				for (uint i = 0; i < cur->nlist; i++)
				{
					cur->inAmp[i].leftMute.type[datatype] = 0;
					cur->inAmp[i].leftGain.type[datatype] = vol;
					cur->inAmp[i].rightMute.type[datatype] = 0;
					cur->inAmp[i].rightGain.type[datatype] = vol;
				}
			}
			if (cur->type == Wpin)
			{
				cur->defaultDev.type[datatype] = 2;
				cur->isOut.type[datatype] = TRUE;
				cur->isIn.type[datatype] = cur->isIn.type[oldData];
			}

			cur = cur->out;
		}
		return 0;
	}
	else if (datatype == inData)
	{
		result = srchWidget(startp, endp, 0, Wain);
		if (result == NULL)
		{
			DBG_TRACE("initRetask", "Can't find retask path");
			return -1;
		}
		DbgPrint("[initRetask] : Retask path to Widget #%X was found", result->id.nid);

		while (cur != NULL)
		{
			if (cur->isOutAmp)
			{
				cur->outAmp.leftMute.type[datatype] = 0;
				cur->outAmp.leftGain.type[datatype] = vol;
				cur->outAmp.rightMute.type[datatype] = 0;
				cur->outAmp.rightGain.type[datatype] = vol;
			}
			if (cur->isInAmp)
			{
				for (uint i = 0; i < cur->nlist; i++)
				{
					cur->inAmp[i].leftMute.type[datatype] = 0;
					cur->inAmp[i].leftGain.type[datatype] = vol;
					cur->inAmp[i].rightMute.type[datatype] = 0;
					cur->inAmp[i].rightGain.type[datatype] = vol;
				}
			}
			if (cur->type == Wpin)
			{
				cur->defaultDev.type[datatype] = 0xa;
				cur->isOut.type[datatype] = cur->isIn.type[oldData];
				cur->isIn.type[datatype] = TRUE;
			}

			cur = cur->in;
		}
		return 0;
	}
	
	return -1;
}

void addConnects(Widget* w, uint nid, uint b)
{
	Widget* src;

	src = NULL;
	if (nid < MAX_WIDGET)
		src = w->fg->codec->widgets[nid];
	if (src == NULL || (src->fg != w->fg))
	{
		DbgPrint("[addConnects] : Invalid connection\n");
		src = NULL;
	}
	if ((w->nlist % 16) == 0)
	{
		Widget **p, **r = NULL;

		if ((p = (Widget**)ExAllocatePool(NonPagedPool, sizeof(Widget*) * (w->nlist + 16))) == NULL)
		{
			DbgPrint("[addConnects] : No memory for widget list\n");
			return;
		}
		r = w->list;
		w->list = p;

		if (r != NULL)
			ExFreePool(r);
	}
	DbgPrint("[addConnects] Connect #%X(%d bit) is Widget #%X\n", w->nlist, b, nid);
	w->list[w->nlist++] = src;
}

static void enumConnects(Widget* w)
{
	uint r, f, b, m, i, n, x, y;

	DbgPrint("----------------CONNECTION #%X--------------\n", w->id.nid);

	r = Cmd(w->id, Getparm, Connlistlen);
	n = r & 0x7f;
	b = (r & 0x80) ? 16 : 8;
	m = (1 << b) - 1;
	f = (32 / b) - 1;
	x = 0;
	for (i = 0; i < n; i++)
	{
		if (i & f)
			r >>= b;
		else
			r = Cmd(w->id, Getconnlist, i);
		y = r & (m >> 1);
		if (i && (r & m) != y)
			while (++x < y)
				addConnects(w, x, b);
		addConnects(w, y, b);
		x = y;
	}
	DbgPrint("-----------------%d CONNECTS----------------\n", w->nlist);
}

static void enumWidget(Widget* w)
{
	uint cap = Cmd(w->id, Getparm, Widgetcap);

	w->type = (cap >> 20) & 0x7;
	w->isConnList = (cap >> 8) & 1;
	w->isOutAmp = (cap >> 2) & 1;
	w->isInAmp = (cap >> 1) & 1;
	w->isStereo = cap & 1;
	
	DbgPrint("================================WIDGET #%X================================\n", w->id.nid);
	DbgPrint("[enumWidget] %s widget, Out Amp %s, In Amp %s, %s\n",
		widgetType[w->type],
		w->isOutAmp ? "present" : "not present",
		w->isInAmp ? "present" : "not present",
		w->isStereo ? "Stereo" : "Mono");

	if (w->isConnList)
		enumConnects(w);

	if (w->isInAmp)
		w->inAmp = (Amp*)ExAllocatePool(NonPagedPool, sizeof(Amp) * w->nlist);

	getWidget(w, oldData);
}

static Funcgroup* enumFuncgroup(Id id)
{
	Funcgroup* fg;
	Widget *w;
	uint i, r, n, base;

	r = Cmd(id, Getparm, Fungrtype) & 0x7f;
	if (r != Graudio)
		return NULL;

	r = Cmd(id, Getparm, Subnodecnt);
	n = r & 0xff;
	base = (r >> 16) & 0xff;

	if (base >= MAX_WIDGET)
	{
		DbgPrint("[enumFuncgroup] : Base %X out of range\n", base);
		return NULL;
	}
	if (base + n > MAX_WIDGET)
	{
		DbgPrint("[enumFuncgroup] : Widgets #%X ~ #%X out of range\n", base, base + n);
		n = MAX_WIDGET - base;
	}
	fg = (Funcgroup*)ExAllocatePool(NonPagedPool, sizeof(Funcgroup));
	if (fg == NULL)
	{
		DbgPrint("[enumFuncgroup] : Out of memory\n");
		return NULL;
	}
	memset(fg, 0, sizeof(Funcgroup));

	fg->codec = &((*myaud).codec);
	fg->id = id;
	fg->type = r;
	fg->base = base;

	DbgPrint("[enumFuncgroup] : Funcgroup %X, Type %X, Subnodebase %X, Subnodecnt %X",
		fg->id.nid, fg->type, base, n);

	for (i = n; i--;)
	{
		Id newid;
		newid.codec = id.codec;
		newid.nid = base + i;
		
		w = (Widget*)ExAllocatePool(NonPagedPool, sizeof(Widget));
		memset(w, 0, sizeof(Widget));

		w->id = newid;
		w->fg = fg;

		(*myaud).codec.widgets[base + i] = w;
		fg->nWidgets++;
	}

	for (i = 0; i < n; i++)
		enumWidget((*myaud).codec.widgets[base + i]);

	DbgPrint("==========================================================================\n");

	return fg;
}

static int enumCodec(Id id)
{
	Funcgroup* fg;
	uint r, n, base;
	uint vid, rid;

	if (sendCmd(id, Getparm, Vendorid, &vid) < 0)
		return -1;
	if (sendCmd(id, Getparm, Revid, &rid) < 0)
		return -1;

	(*myaud).codec.id = id;
	(*myaud).codec.vid = vid;
	(*myaud).codec.rid = rid;

	r = Cmd(id, Getparm, Subnodecnt);
	n = r & 0xff;
	base = (r >> 16) & 0xff;

	DbgPrint("[enumCodec] : Codec %X, Vendor %X, Revision %X, Subnodebase %X, Subnodecnt %X", 
		(*myaud).codec.id.codec, (*myaud).codec.vid, (*myaud).codec.rid, base, n);

	for (uint i = 0; i < n; i++)
	{
		Id newid;
		newid.codec = id.codec;
		newid.nid = base + i;
		fg = enumFuncgroup(newid);
		if (fg == NULL)
			continue;
		fg->next = (*myaud).codec.fgroup;
		(*myaud).codec.fgroup = fg;
	}
	if ((*myaud).codec.fgroup == NULL)
		return -1;

	return 0;

}

int enumDevice()
{
	Id id;
	id.nid = 0;

	for (uint i = 0; i < MAX_CODEC; i++)
	{
		id.codec = i;
		if (enumCodec(id) == 0)
			return 0;
	}
	return -1;
}