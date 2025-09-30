#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MULTIFILTER_URI "https://github.com/lilbrimstone/multifilter"
#define NUM_FORMANTS 3
#define COMB_BUFFER_SIZE 4096

const double VOWEL_DATA[5][NUM_FORMANTS][2] = {
    {{730,8},{1090,9},{2440,10}},{{270,8},{2290,12},{3010,12}},{{390,9},{1990,10},{2550,10}},
    {{570,8},{840,9},{2410,10}},{{300,8},{870,9},{2240,10}}
};

typedef enum {
    PORT_IN_L=0, PORT_OUT_L=1, PORT_OUT_R=2, PORT_CUTOFF=3, PORT_RESONANCE=4,
    PORT_MIX=5, PORT_FILTER_TYPE=6, PORT_VOWEL_SELECT=7, PORT_LFO_RATE=8,
    PORT_LFO_SHAPE=9, PORT_CUTOFF_MOD=10, PORT_RES_MOD=11,
    PORT_LFO_SLEW=12, PORT_LFO_STEPS=13
} PortIndex;

typedef struct {
    double bq_z1, bq_z2, svf_lp_z1, svf_bp_z1;
    double tb303_v1, tb303_v2, tb303_v3, tb303_v4;
    double formant_z1[NUM_FORMANTS], formant_z2[NUM_FORMANTS];
    double* comb_buffer;
    int     comb_write_pos;
    double rm_phase;
    double lfo_phase, lfo_sh_value, lfo_slew_z1, lfo_stepped_value;
    int    lfo_step_counter;

    const float* p_cutoff, *p_resonance, *p_mix, *p_filter_type, *p_vowel_select;
    const float* p_lfo_rate, *p_lfo_shape, *p_cutoff_mod, *p_res_mod;
    const float* p_lfo_slew, *p_lfo_steps;
    const float* input_l;
    float*       output_l, *output_r;
    double sample_rate;
} MultiFilter;

static LV2_Handle
instantiate(const LV2_Descriptor* d, double rate, const char* path, const LV2_Feature* const* f) {
    MultiFilter* self = calloc(1, sizeof(MultiFilter));
    if (!self) return NULL;
    self->sample_rate = rate;
    self->comb_buffer = calloc(COMB_BUFFER_SIZE, sizeof(double));
    if (!self->comb_buffer) { free(self); return NULL; }
    return self;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void* data) {
    MultiFilter* self = (MultiFilter*)instance;
    switch ((PortIndex)port) {
        case PORT_IN_L: self->input_l = data; break;
        case PORT_OUT_L: self->output_l = data; break;
        case PORT_OUT_R: self->output_r = data; break;
        case PORT_CUTOFF: self->p_cutoff = data; break;
        case PORT_RESONANCE: self->p_resonance = data; break;
        case PORT_MIX: self->p_mix = data; break;
        case PORT_FILTER_TYPE: self->p_filter_type = data; break;
        case PORT_VOWEL_SELECT: self->p_vowel_select = data; break;
        case PORT_LFO_RATE: self->p_lfo_rate = data; break;
        case PORT_LFO_SHAPE: self->p_lfo_shape = data; break;
        case PORT_CUTOFF_MOD: self->p_cutoff_mod = data; break;
        case PORT_RES_MOD: self->p_res_mod = data; break;
        case PORT_LFO_SLEW: self->p_lfo_slew = data; break;
        case PORT_LFO_STEPS: self->p_lfo_steps = data; break;
    }
}

static void
activate(LV2_Handle instance) {
    MultiFilter* self = (MultiFilter*)instance;
    self->bq_z1 = self->bq_z2 = 0.0;
    self->svf_lp_z1 = self->svf_bp_z1 = 0.0;
    self->tb303_v1=self->tb303_v2=self->tb303_v3=self->tb303_v4=0.0;
    // <-- FIX: Changed self.formant_z2 to self->formant_z2
    for (int i=0; i<NUM_FORMANTS; ++i) self->formant_z1[i] = self->formant_z2[i] = 0.0;
    if (self->comb_buffer) memset(self->comb_buffer, 0, COMB_BUFFER_SIZE*sizeof(double));
    self->comb_write_pos = 0; self->rm_phase = 0.0; self->lfo_phase = 0.0;
    self->lfo_sh_value = 0.0; self->lfo_slew_z1 = 0.0;
    self->lfo_step_counter = 0; self->lfo_stepped_value = 0.0;
}


static void
run(LV2_Handle instance, uint32_t n_samples) {
    MultiFilter* self = (MultiFilter*)instance;
    if (!self->p_cutoff || !self->p_lfo_rate || !self->p_lfo_slew) return;

    const float base_cutoff_hz = *self->p_cutoff, base_resonance = *self->p_resonance;
    const float mix = *self->p_mix;
    const int filter_type = (int)(*self->p_filter_type + 0.5f);
    const int vowel_select = (int)(*self->p_vowel_select + 0.5f);
    const float lfo_rate_hz = *self->p_lfo_rate, lfo_slew_knob = *self->p_lfo_slew, lfo_steps_knob = *self->p_lfo_steps;
    const int lfo_shape = (int)(*self->p_lfo_shape + 0.5f);
    const float cutoff_mod_amount = *self->p_cutoff_mod, res_mod_amount = *self->p_res_mod;
    
    const double lfo_phase_inc = 2.0 * M_PI * lfo_rate_hz / self->sample_rate;
    const double smoothing_factor = pow(0.001, lfo_slew_knob);
    const int step_hold_period = 1 + (int)(lfo_steps_knob * lfo_steps_knob * (self->sample_rate * 0.5));

    for (uint32_t i = 0; i < n_samples; ++i) {
        double lfo_raw = 0.0;
        switch (lfo_shape) {
            case 0: lfo_raw = (sin(self->lfo_phase) + 1.0) * 0.5; break;
            case 1: lfo_raw = (self->lfo_phase < M_PI) ? self->lfo_phase / M_PI : 2.0 - (self->lfo_phase / M_PI); break;
            case 2: lfo_raw = self->lfo_phase / (2.0 * M_PI); break;
            case 3: lfo_raw = 1.0 - (self->lfo_phase / (2.0 * M_PI)); break;
            case 4: lfo_raw = (self->lfo_phase < M_PI) ? 1.0 : 0.0; break;
            case 5: lfo_raw = self->lfo_sh_value; break;
        }

        self->lfo_phase += lfo_phase_inc;
        if (self->lfo_phase >= 2.0 * M_PI) {
            self->lfo_phase -= 2.0 * M_PI;
            if (lfo_shape == 5) self->lfo_sh_value = (double)rand() / RAND_MAX;
        }

        self->lfo_step_counter++;
        if (self->lfo_step_counter >= step_hold_period) {
            self->lfo_step_counter = 0;
            self->lfo_stepped_value = lfo_raw;
        }
        double lfo_after_steps = (lfo_steps_knob > 0.001) ? self->lfo_stepped_value : lfo_raw;

        self->lfo_slew_z1 += (lfo_after_steps - self->lfo_slew_z1) * smoothing_factor;
        double final_lfo_val = self->lfo_slew_z1;

        double modulated_cutoff = base_cutoff_hz + final_lfo_val * cutoff_mod_amount;
        double modulated_res = base_resonance + final_lfo_val * res_mod_amount;

        if (modulated_cutoff < 20.0) modulated_cutoff = 20.0;
        if (modulated_cutoff > self->sample_rate * 0.49) modulated_cutoff = self->sample_rate * 0.49;
        if (modulated_res < 0.707) modulated_res = 0.707;
        if (modulated_res > 20.0) modulated_res = 20.0;

        const float in = self->input_l[i];
        float wet = 0.0f;
        
        const double w0_bq = 2.0*M_PI*modulated_cutoff/self->sample_rate, alpha_bq = sin(w0_bq)/(2.0*modulated_res), cos_w0_bq = cos(w0_bq);
        const double g_svf = tan(M_PI*modulated_cutoff/self->sample_rate), R_svf = 1.0/(2.0*modulated_res);
        const double g_303 = 1.0 - exp(-2.0*M_PI*modulated_cutoff/self->sample_rate), k_303 = (modulated_res-0.707)/19.293*4.2;
        if (filter_type == 0) { // Biquad
            double b0=(1-cos_w0_bq)*0.5,b1=1-cos_w0_bq,b2=b0,a0=1+alpha_bq,a1=-2*cos_w0_bq,a2=1-alpha_bq;
            const double inv_a0=1/a0;b0*=inv_a0;b1*=inv_a0;b2*=inv_a0;a1*=inv_a0;a2*=inv_a0;
            const double out_f=b0*in+self->bq_z1;self->bq_z1=(b1*in-a1*out_f)+self->bq_z2;self->bq_z2=b2*in-a2*out_f;
            wet=out_f;
        } else if (filter_type == 1) { // SEM
            const double hp=(in-self->svf_lp_z1-(2*R_svf*self->svf_bp_z1))/(1+(2*R_svf*g_svf)+g_svf*g_svf);
            const double bp=(g_svf*hp)+self->svf_bp_z1;const double lp=(g_svf*bp)+self->svf_lp_z1;
            self->svf_bp_z1=bp;self->svf_lp_z1=lp;wet=lp;
        } else if (filter_type == 2) { // 303
            const double u=in-k_303*self->tb303_v4;self->tb303_v1+=g_303*(tanh(u)-tanh(self->tb303_v1));
            self->tb303_v2+=g_303*(tanh(self->tb303_v1)-tanh(self->tb303_v2));self->tb303_v3+=g_303*(tanh(self->tb303_v2)-tanh(self->tb303_v3));
            self->tb303_v4+=g_303*(tanh(self->tb303_v3)-tanh(self->tb303_v4));wet=self->tb303_v4;
        } else if (filter_type == 3) { // Vowel
            double total_out=0;const double freq_shift=pow(2,(modulated_cutoff-1000)/1000);
            const double norm_res=(modulated_res-0.707)/19.293,gain_comp=1.0+norm_res*3.0;
            for(int j=0;j<NUM_FORMANTS;++j){
                double f_hz=VOWEL_DATA[vowel_select][j][0]*freq_shift;
                if(f_hz>self->sample_rate*0.49)f_hz=self->sample_rate*0.49;
                const double f_q=VOWEL_DATA[vowel_select][j][1]*modulated_res*0.5,w0=2*M_PI*f_hz/self->sample_rate,alpha=sin(w0)/(2*f_q);
                double b0=alpha,b1=0,b2=-alpha,a0=1+alpha,a1=-2*cos(w0),a2=1-alpha;const double inv_a0=1/a0;
                b0*=inv_a0;b1*=inv_a0;b2*=inv_a0;a1*=inv_a0;a2*=inv_a0;
                const double out_f=b0*in+self->formant_z1[j];self->formant_z1[j]=(b1*in-a1*out_f)+self->formant_z2[j];
                self->formant_z2[j]=b2*in-a2*out_f;total_out+=out_f;
            }
            wet=total_out*gain_comp;
        } else if (filter_type == 4) { // Comb
            double delay_samps=self->sample_rate/modulated_cutoff;
            if(delay_samps>=COMB_BUFFER_SIZE-1)delay_samps=COMB_BUFFER_SIZE-1;if(delay_samps<1.0)delay_samps=1.0;
            double norm_res=(modulated_res-0.707)/19.293,feedback=norm_res*0.99;
            double read_pos_f=self->comb_write_pos-delay_samps;
            int read_pos_i1=(int)floor(read_pos_f),read_pos_i2=read_pos_i1+1;double frac=read_pos_f-read_pos_i1;
            double s1=self->comb_buffer[read_pos_i1&(COMB_BUFFER_SIZE-1)],s2=self->comb_buffer[read_pos_i2&(COMB_BUFFER_SIZE-1)];
            double interp_s=s1+frac*(s2-s1);wet=in+feedback*interp_s;
            self->comb_buffer[self->comb_write_pos]=wet;self->comb_write_pos=(self->comb_write_pos+1)&(COMB_BUFFER_SIZE-1);
        } else { // filter_type == 5, Ring Mod
            const double norm_res=(modulated_res-0.707)/19.293;
            const double mod_rm=sin(self->rm_phase), mod_am=(mod_rm+1.0)*0.5;
            const double final_modulator=(1.0-norm_res)*mod_am+norm_res*mod_rm;
            wet=in*final_modulator;
            const double phase_inc=2.0*M_PI*modulated_cutoff/self->sample_rate;
            self->rm_phase+=phase_inc; if(self->rm_phase>=2.0*M_PI)self->rm_phase-=2.0*M_PI;
        }

        const float out = (mix * wet) + ((1.0f - mix) * in);
        self->output_l[i] = out;
        self->output_r[i] = out;
    }
}

static void
deactivate(LV2_Handle instance) {}

static void
cleanup(LV2_Handle instance) {
    MultiFilter* self = (MultiFilter*)instance;
    if (self && self->comb_buffer) free(self->comb_buffer);
    free(instance);
}

static const LV2_Descriptor descriptor = {
    MULTIFILTER_URI, instantiate, connect_port, activate,
    run, deactivate, cleanup, NULL
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    switch (index) { case 0: return &descriptor; default: return NULL; }
}