// LLC converter calculation helper

#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

#define TARGET_Q 0.4        // Recommended default quality factor
#define DEFAULT_LAMBDA 6.0  // Recommended default inductance ratio Lm/Lr

typedef enum {
    HALF_BRIDGE = 0,
    FULL_BRIDGE  = 1
} BridgeType;

// ─────────────────────────────────────────────
//  Structs
// ─────────────────────────────────────────────
typedef struct {
    // User inputs
    double v_in;
    double v_out;
    double power;
    double switching_frequency;
    double lambda;          // Lm/Lr — user design choice
    BridgeType bridge_type;

    // Derived values
    double i_out;
    double turns_ratio;             // n = Np/Ns  (step-down convention)
    double load_resistance;         // Rload
    double reflected_load;          // Rac
    double quality_factor;          // Q  (derived, not entered)
    double characteristic_impedance;// Zr
    double resonant_inductance;     // Lr
    double resonant_capacitance;    // Cr
    double magnetizing_inductance;  // Lm
    double inductance_ratio;        // gamma = Lr/Lm  (used in gain formula)
    double resonant_frequency;      // fr
    double normalized_frequency;    // fn = fsw/fr

    // Gains
    double desired_gain;
    double bridge_gain;
    double tank_gain;
    double transformer_gain;
    double total_gain;
    double predicted_vout;
} LLC_Converter;

// ─────────────────────────────────────────────
//  Input Validation
// ─────────────────────────────────────────────
int validateInputs(LLC_Converter *c) {
    int valid = 1;
    if (c->v_in <= 0)               { printf("  ERROR: Vin must be > 0\n");              valid = 0; }
    if (c->v_out <= 0)              { printf("  ERROR: Vout must be > 0\n");             valid = 0; }
    if (c->v_out >= c->v_in)        { printf("  WARNING: LLC typically steps down (Vout < Vin)\n"); }
    if (c->power <= 0)              { printf("  ERROR: Power must be > 0\n");            valid = 0; }
    if (c->switching_frequency <= 0){ printf("  ERROR: Frequency must be > 0\n");       valid = 0; }
    if (c->lambda < 3 || c->lambda > 15)
                                    { printf("  WARNING: Lambda outside typical range 3-15\n"); }
    return valid;
}

// ─────────────────────────────────────────────
//  General / Gain Functions
// ─────────────────────────────────────────────
double calculateBridgeGain(BridgeType bt) {
    return (bt == HALF_BRIDGE) ? 0.5 : 1.0;
}

// Tank gain using Lr/Lm (gamma) convention — matches STM/TI app notes
double calculateResonantTankGain(double fn, double gamma, double Q) {
    double term1 = pow(1.0 + gamma - (gamma / (fn * fn)), 2.0);
    double term2 = Q * Q * pow(fn - (1.0 / fn), 2.0);
    return 1.0 / sqrt(term1 + term2);
}

// ─────────────────────────────────────────────
//  Core Calculation
// ─────────────────────────────────────────────
void calculateLLC(LLC_Converter *c) {
    // Output current
    c->i_out = c->power / c->v_out;

    // Turns ratio n = Np/Ns (step-down: n > 1 for Vout < Vin/2)
    double bridge_divisor = (c->bridge_type == HALF_BRIDGE) ? 2.0 : 1.0;
    c->turns_ratio = c->v_in / (bridge_divisor * c->v_out);

    // Load
    c->load_resistance = (c->v_out * c->v_out) / c->power;

    // Reflected load — embeds n² and rectifier factor (8/π²)
    c->reflected_load = (8.0 * c->turns_ratio * c->turns_ratio / (M_PI * M_PI))
                        * c->load_resistance;

    // Use fixed target Q to decouple Lr and Cr
    c->quality_factor = TARGET_Q;
    c->characteristic_impedance = c->quality_factor * c->reflected_load;

    // Resonant components — fr = fsw at design point
    c->resonant_frequency   = c->switching_frequency;
    c->resonant_inductance  = c->characteristic_impedance
                              / (2.0 * M_PI * c->resonant_frequency);
    c->resonant_capacitance = 1.0
                              / (2.0 * M_PI * c->resonant_frequency
                                 * c->characteristic_impedance);

    // Magnetizing inductance from user-supplied lambda (Lm/Lr)
    c->magnetizing_inductance = c->lambda * c->resonant_inductance;

    // gamma = Lr/Lm  (convention used in gain formula)
    c->inductance_ratio = 1.0 / c->lambda;

    // Normalised frequency — should be 1.0 at design point
    c->normalized_frequency = c->switching_frequency / c->resonant_frequency;

    // ── Gains ──
    c->desired_gain     = c->v_out / c->v_in;
    c->bridge_gain      = calculateBridgeGain(c->bridge_type);
    c->transformer_gain = 1.0 / c->turns_ratio;   // Ns/Np
    c->tank_gain        = calculateResonantTankGain(c->normalized_frequency,
                                                     c->inductance_ratio,
                                                     c->quality_factor);
    c->total_gain       = c->bridge_gain * c->tank_gain * c->transformer_gain;
    c->predicted_vout   = c->v_in * c->total_gain;
}

// ─────────────────────────────────────────────
//  ZVS Check
// ─────────────────────────────────────────────
void checkZVS(LLC_Converter *c, double t_dead_ns, double C_oss_pF) {
    double t_dead = t_dead_ns * 1e-9;
    double C_oss  = C_oss_pF  * 1e-12;
    double Lm_max = t_dead / (8.0 * c->switching_frequency * C_oss);

    printf("\n--- ZVS Check ---\n");
    printf("  Lm designed:  %.4f uH\n", c->magnetizing_inductance * 1e6);
    printf("  Lm max (ZVS): %.4f uH\n", Lm_max * 1e6);
    if (c->magnetizing_inductance <= Lm_max)
        printf("  ZVS: PASS ✓\n");
    else
        printf("  ZVS: FAIL — reduce Lm or increase deadtime\n");
}

// ─────────────────────────────────────────────
//  Gain Curve CSV Export
// ─────────────────────────────────────────────
void exportGainCurve(LLC_Converter *c, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) { printf("Could not open %s for writing\n", filename); return; }

    double Q_full  = c->quality_factor;          // full load
    double Q_half  = c->quality_factor * 0.5;    // half load
    double Q_light = c->quality_factor * 0.1;    // light load
    double gamma   = c->inductance_ratio;

    fprintf(f, "fn,Q_full(%.2f),Q_half(%.2f),Q_light(%.2f)\n",
            Q_full, Q_half, Q_light);

    for (double fn = 0.3; fn <= 2.0; fn += 0.01) {
        double g_full  = calculateResonantTankGain(fn, gamma, Q_full);
        double g_half  = calculateResonantTankGain(fn, gamma, Q_half);
        double g_light = calculateResonantTankGain(fn, gamma, Q_light);
        fprintf(f, "%.3f,%.6f,%.6f,%.6f\n", fn, g_full, g_half, g_light);
    }
    fclose(f);
    printf("\n  Gain curve exported to: %s\n", filename);
}

// ─────────────────────────────────────────────
//  Print Results  (writes to any FILE* — stdout or file)
// ─────────────────────────────────────────────
void fprintResults(FILE *out, LLC_Converter *c) {
    fprintf(out, "\n========= LLC Design Results =========\n");
    fprintf(out, "  Bridge type:              %s\n",
           c->bridge_type == HALF_BRIDGE ? "Half Bridge" : "Full Bridge");
    fprintf(out, "\n  -- User Inputs --\n");
    fprintf(out, "  Vin:                      %.4f V\n",   c->v_in);
    fprintf(out, "  Vout:                     %.4f V\n",   c->v_out);
    fprintf(out, "  Power:                    %.4f W\n",   c->power);
    fprintf(out, "  Switching frequency:      %.3f kHz\n", c->switching_frequency / 1e3);
    fprintf(out, "  Lambda (Lm/Lr):           %.4f\n",     c->lambda);
    fprintf(out, "\n  -- Derived Values --\n");
    fprintf(out, "  Output current (Iout):    %.4f A\n",   c->i_out);
    fprintf(out, "  Turns ratio (Np:Ns):      %.3f : 1\n", c->turns_ratio);
    fprintf(out, "  Load resistance (Rload):  %.4f Ohm\n", c->load_resistance);
    fprintf(out, "  Reflected load (Rac):     %.4f Ohm\n", c->reflected_load);
    fprintf(out, "  Quality factor (Q):       %.4f\n",     c->quality_factor);
    fprintf(out, "  Characteristic imp (Zr):  %.4f Ohm\n", c->characteristic_impedance);
    fprintf(out, "  Resonant freq (fr):       %.3f kHz\n", c->resonant_frequency / 1e3);
    fprintf(out, "  Resonant inductance (Lr): %.4f uH\n",  c->resonant_inductance * 1e6);
    fprintf(out, "  Resonant capacitance(Cr): %.4f nF\n",  c->resonant_capacitance * 1e9);
    fprintf(out, "  Magnetizing ind (Lm):     %.4f uH\n",  c->magnetizing_inductance * 1e6);
    fprintf(out, "  Gamma (Lr/Lm):            %.4f\n",     c->inductance_ratio);
    fprintf(out, "  Normalised freq (fn):     %.4f\n",     c->normalized_frequency);
    fprintf(out, "\n  -- Gain Breakdown --\n");
    fprintf(out, "  Desired gain:             %.4f\n",     c->desired_gain);
    fprintf(out, "  Bridge gain:              %.4f\n",     c->bridge_gain);
    fprintf(out, "  Tank gain:                %.4f\n",     c->tank_gain);
    fprintf(out, "  Transformer gain:         %.4f\n",     c->transformer_gain);
    fprintf(out, "  Total gain:               %.4f\n",     c->total_gain);
    fprintf(out, "  Predicted Vout:           %.4f V\n",   c->predicted_vout);
    fprintf(out, "  Target Vout:              %.4f V\n",   c->v_out);
    fprintf(out, "  Gain error:               %.3f%%\n",
           fabs(c->predicted_vout - c->v_out) / c->v_out * 100.0);
    fprintf(out, "======================================\n");
}

void printResults(LLC_Converter *c) {
    fprintResults(stdout, c);
}

// ─────────────────────────────────────────────
//  Save Results to Text File
// ─────────────────────────────────────────────
void saveResults(LLC_Converter *c, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        printf("  ERROR: Could not open %s for writing\n", filename);
        return;
    }

    // Header with timestamp
    time_t now = time(NULL);
    fprintf(f, "LLC Converter Design Report\n");
    fprintf(f, "Generated: %s", ctime(&now));

    fprintResults(f, c);
    fclose(f);
    printf("  Design results saved to: %s\n", filename);
}

// ─────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────
int main(void) {
    LLC_Converter conv = {0};

    printf("=== LLC Converter Design Calculator ===\n\n");

    printf("Enter bridge type (0 = Half Bridge, 1 = Full Bridge): ");
    scanf("%d", (int*)&conv.bridge_type);

    printf("Enter input voltage Vin (V): ");
    scanf("%lf", &conv.v_in);

    printf("Enter desired output voltage Vout (V): ");
    scanf("%lf", &conv.v_out);

    printf("Enter output power (W): ");
    scanf("%lf", &conv.power);

    printf("Enter switching frequency (Hz): ");
    scanf("%lf", &conv.switching_frequency);

    printf("Enter inductance ratio lambda = Lm/Lr (recommended 4-8, default %.1f): ", DEFAULT_LAMBDA);
    scanf("%lf", &conv.lambda);
    if (conv.lambda <= 0) conv.lambda = DEFAULT_LAMBDA;

    printf("\n--- Input Validation ---\n");
    if (!validateInputs(&conv)) {
        printf("Fix errors above and retry.\n");
        return 1;
    }

    // Note: Q is fixed internally at TARGET_Q = 0.4
    printf("  [Q fixed internally at %.2f — reported as output]\n", TARGET_Q);

    calculateLLC(&conv);
    printResults(&conv);

    // Optional ZVS check
    double t_dead, C_oss;
    printf("\nRun ZVS check? (enter deadtime ns and Coss pF, or 0 0 to skip): ");
    scanf("%lf %lf", &t_dead, &C_oss);
    if (t_dead > 0 && C_oss > 0)
        checkZVS(&conv, t_dead, C_oss);

    // Optional save results to text file
    char save_choice;
    printf("\nSave design results to text file? (y/n): ");
    scanf(" %c", &save_choice);
    if (save_choice == 'y' || save_choice == 'Y') {
        char filename[256];
        printf("Enter filename (leave blank for 'llc_design.txt'): ");
        scanf(" %[^\n]", filename);
        if (strlen(filename) == 0)
            saveResults(&conv, "llc_design.txt");
        else
            saveResults(&conv, filename);
    }

    // Optional gain curve export
    char choice;
    printf("\nExport gain curve to CSV? (y/n): ");
    scanf(" %c", &choice);
    if (choice == 'y' || choice == 'Y')
        exportGainCurve(&conv, "gain_curve.csv");

    return 0;
}