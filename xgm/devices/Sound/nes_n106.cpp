#include <cstring>
#include "nes_n106.h"

namespace xgm {

NES_N106::NES_N106 ()
{
    option[OPT_SERIAL] = 0;
    option[OPT_PHASE_READ_ONLY] = 0;
    option[OPT_LIMIT_WAVELENGTH] = 0;
    SetClock (DEFAULT_CLOCK);
    SetRate (DEFAULT_RATE);
    for (int i=0; i < 8; ++i)
    {
        sm[0][i] = 128;
        sm[1][i] = 128;
    }
    Reset();
}

NES_N106::~NES_N106 ()
{
}

void NES_N106::SetStereoMix (int trk, INT16 mixl, INT16 mixr)
{
    if (trk < 0 || trk >= 8) return;
    trk = 7-trk; // displayed channels are inverted
    sm[0][trk] = mixl;
    sm[1][trk] = mixr;
}

ITrackInfo *NES_N106::GetTrackInfo (int trk)
{
    int channels = get_channels();
    int channel = 7-trk; // invert the track display

    TrackInfoN106* t = &trkinfo[channel];

    if (trk >= channels)
    {
        t->max_volume = 15;
        t->volume = 0;
        t->_freq = 0;
        t->wavelen = 0;
        t->tone = -1;
        t->output = 0;
        t->key = false;
        t->freq = 0;
    }
    else
    {
        t->max_volume = 15;
        t->volume = get_vol(channel);
        t->_freq = get_freq(channel);
        t->wavelen = get_len(channel);
        t->tone = get_off(channel);
        t->output = fout[channel];

        t->key = (t->volume > 0) && (t->_freq > 0);
        t->freq = (double(t->_freq) * clock) / double(15 * 65536 * channels * t->wavelen);

        for (int i=0; i < t->wavelen; ++i)
            t->wave[i] = get_sample((i+t->tone)&0xFF);
    }

    return t;
}

void NES_N106::SetClock (double c)
{
    clock = c;
}

void NES_N106::SetRate (double r)
{
    rate = r;
}

void NES_N106::SetMask (int m)
{
    // bit reverse the mask,
    // N163 waves are displayed in reverse order
    mask = 0
        | ((m & (1<<0)) ? (1<<7) : 0)
        | ((m & (1<<1)) ? (1<<6) : 0)
        | ((m & (1<<2)) ? (1<<5) : 0)
        | ((m & (1<<3)) ? (1<<4) : 0)
        | ((m & (1<<4)) ? (1<<3) : 0)
        | ((m & (1<<5)) ? (1<<2) : 0)
        | ((m & (1<<6)) ? (1<<1) : 0)
        | ((m & (1<<7)) ? (1<<0) : 0);
}

void NES_N106::SetOption (int id, int val)
{
    if (id<OPT_END) option[id] = val;
}

void NES_N106::Reset ()
{
    master_disable = false;
    ::memset(reg, 0, sizeof(reg));
    reg_select = 0;
    reg_advance = false;
    tick_channel = 0;
    tick_clock = 0;
    render_channel = 0;
    render_clock = 0;
    render_subclock = 0;

    for (int i=0; i<8; ++i) fout[i] = 0;

    Write(0xE000, 0x00); // master disable off
    Write(0xF800, 0x80); // select $00 with auto-increment
    for (unsigned int i=0; i<0x80; ++i) // set all regs to 0
    {
        Write(0x4800, 0x00);
    }
    Write(0xF800, 0x00); // select $00 without auto-increment
}

double NES_N106::linear_approximate(double now_a, double min_a, double max_a, double min_b, double max_b) {
	if (now_a < min_a) {
		now_a = min_a;
	}
	else if (now_a > max_a) {
		now_a = max_a;
	}

	return ((now_a - min_a) * (max_b - min_b)) / (max_a - min_a) + min_b;
}

void NES_N106::Tick (UINT32 clocks)
{
    if (master_disable) return;

    int channels = get_channels();

    tick_clock += clocks;
    render_clock += clocks; // keep render in sync
    while (tick_clock > 0)
    {
        int channel = 7-tick_channel;

        UINT32 phase = get_phase(channel);
        UINT32 freq  = get_freq(channel);
        UINT32 len   = get_len(channel);
        UINT32 off   = get_off(channel);
        INT32  vol   = get_vol(channel);

		

        // accumulate 24-bit phase
        phase = (phase + freq) & 0x00FFFFFF;

        // wrap phase if wavelength exceeded
        UINT32 hilen = len << 16;
        while (phase >= hilen) phase -= hilen;

        // write back phase
        set_phase(phase, channel);

        // fetch sample (note: N163 output is centred at 8, and inverted w.r.t 2A03)
        // INT32 sample = 8 - get_sample(((phase >> 16) + off) & 0xFF);
        // fout[channel] = sample * vol;

        // cycle to next channel every 15 clocks
        tick_clock -= 15;
        ++tick_channel;
        if (tick_channel >= channels)
            tick_channel = 0;
    }

	for (int channel = 7; channel >= (8 - channels); channel--) {
		double phase = chphase2[channel];
		double freq = (double)get_freq(channel);
		double len = (double)get_len(channel) * 65536.0;
		double off = get_off(channel);
		double vol = get_vol(channel);

		phase += (((1.0 / 15.0) / (double)channels) * freq) * (double)clocks;
		if (phase >= len) {
			phase -= len;
		}

		chphase2[channel] = phase;

		double index = ((phase / 65536.0) + off);
		if (index > 255.0) {
			index = 255.0;
		}

		if (true) {
			fout[channel] = linear_approximate(
				index, (INT32)index,
				((INT32)index) + 1,

				8.0 - (double)get_sample((INT32)index),
				8.0 - (double)get_sample(((INT32)index) + 1)
			) * (double)vol;
		}
		else {
			fout[channel] = (8.0 - (double)get_sample((INT32)index)) * (double)vol;
		}

		//double sample = 8.0 - get_sample((INT32)index);
		//fout[channel] = sample * (double)vol;
	}
}

UINT32 NES_N106::Render (INT32 b[2])
{
	b[0] = 0;
	b[1] = 0;
	
	double tempb[2];
	tempb[0] = 0;
	tempb[1] = 0;

	if (master_disable) return 2;

    int channels = get_channels();

    for (int i = (8-channels); i<8; ++i)
    {
        if (0 == ((mask >> i) & 1))
        {
            tempb[0] += fout[i] * (double)sm[0][i];
            tempb[1] += fout[i] * (double)sm[1][i];
        }
    }

    // mix together, increase output level by 8 bits, roll off 7 bits from sm
    double MIX[9] = { 256.0/1.0, 256.0/1.0, 256.0/2.0, 256.0/3.0, 256.0/4.0, 256.0/5.0, 256.0/6.0, 256.0/6, 256.0/6.0 };
    tempb[0] = (tempb[0] * MIX[channels]) / 128.0;
    tempb[1] = (tempb[1] * MIX[channels]) / 128.0;
    // when approximating the serial multiplex as a straight mix, once the
    // multiplex frequency gets below the nyquist frequency an average mix
    // begins to sound too quiet. To approximate this effect, I don't attenuate
    // any further after 6 channels are active.

    // 8 bit approximation of master volume
    // max N163 vol vs max APU square
    // unfortunately, games have been measured as low as 3.4x and as high as 8.5x
    // with higher volumes on Erika, King of Kings, and Rolling Thunder
    // and lower volumes on others. Using 6.0x as a rough "one size fits all".
    const double MASTER_VOL = 6.0 * 1223.0;
    const double MAX_OUT = 15.0 * 15.0 * 256.0; // max digital value
    const double GAIN = int((MASTER_VOL / MAX_OUT) * 256.0f);
    b[0] = (tempb[0] * GAIN) / 256.0;
    b[1] = (tempb[1] * GAIN) / 256.0;

    return 2;
}

bool NES_N106::Write (UINT32 adr, UINT32 val, UINT32 id)
{
    if (adr == 0xE000) // master disable
    {
        master_disable = ((val & 0x40) != 0);
        return true;
    }
    else if (adr == 0xF800) // register select
    {
        reg_select = (val & 0x7F);
        reg_advance = (val & 0x80) != 0;
        return true;
    }
    else if (adr == 0x4800) // register write
    {
        
        reg[reg_select] = val;
        if (reg_advance)
            reg_select = (reg_select + 1) & 0x7F;
        return true;
    }
    return false;
}

bool NES_N106::Read (UINT32 adr, UINT32 & val, UINT32 id)
{
    if (adr == 0x4800) // register read
    {
        val = reg[reg_select];
        if (reg_advance)
            reg_select = (reg_select + 1) & 0x7F;
        return true;
    }
    return false;
}

//
// register decoding/encoding functions
// 

inline UINT32 NES_N106::get_phase (int channel)
{
    // 24-bit phase stored in channel regs 1/3/5
    channel = channel << 3;
    return (reg[0x41 + channel]      )
        +  (reg[0x43 + channel] << 8 )
        +  (reg[0x45 + channel] << 16);
}

inline UINT32 NES_N106::get_freq (int channel)
{
    // 19-bit frequency stored in channel regs 0/2/4
    channel = channel << 3;
    return ( reg[0x40 + channel]              )
        +  ( reg[0x42 + channel]         << 8 )
        +  ((reg[0x44 + channel] & 0x03) << 16);
}

inline UINT32 NES_N106::get_off (int channel)
{
    // 8-bit offset stored in channel reg 6
    channel = channel << 3;
    return reg[0x46 + channel];
}

inline UINT32 NES_N106::get_len (int channel)
{
    // 6-bit<<3 length stored obscurely in channel reg 4
    channel = channel << 3;
    return 256 - (reg[0x44 + channel] & 0xFC);
}

inline INT32 NES_N106::get_vol (int channel)
{
    // 4-bit volume stored in channel reg 7
    channel = channel << 3;
    return reg[0x47 + channel] & 0x0F;
}

inline INT32 NES_N106::get_sample (UINT32 index)
{
    // every sample becomes 2 samples in regs
    return (index&1) ?
        ((reg[index>>1] >> 4) & 0x0F) :
        ( reg[index>>1]       & 0x0F) ;
}

inline int NES_N106::get_channels ()
{
    // 3-bit channel count stored in reg 0x7F
    return ((reg[0x7F] >> 4) & 0x07) + 1;
}

inline void NES_N106::set_phase (UINT32 phase, int channel)
{
    // 24-bit phase stored in channel regs 1/3/5
    channel = channel << 3;
    reg[0x41 + channel] =  phase        & 0xFF;
    reg[0x43 + channel] = (phase >> 8 ) & 0xFF;
    reg[0x45 + channel] = (phase >> 16) & 0xFF;
}

} //namespace
