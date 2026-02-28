// LLC converter calculation helper

#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define TARGET_Q 0.5
#define DEFAULT_LAMBDA 6.0

typedef enum {
    HALF_BRIDGE = 0,
    FULL_BRIDGE  = 1
} BridgeType;

typedef struct {
    // User inputs
    double v_in;
    double v_out;
    double power;
    double switching_frequency;
    double lambda;          // Lr/Lm
    BridgeType bridge_type;

    // Derived values
    double i_out;
    double turns_ratio;             // n = Np/Ns
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

double calculateBridgeGain(BridgeType bt) {
    return (bt == HALF_BRIDGE) ? 0.5 : 1.0;
}
double calculateResonantTankGain(double fn, double gamma, double Q) {
    double term1 = pow(1.0 + gamma - (gamma / (fn * fn)), 2.0);
    double term2 = Q * Q * pow(fn - (1.0 / fn), 2.0);
    return 1.0 / sqrt(term1 + term2);
}

void calculateLLC(LLC_Converter *c){
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
    c->tank_gain        = calculateResonantTankGain(c->normalized_frequency, c->inductance_ratio, c->quality_factor);
    c->total_gain       = c->bridge_gain * c->tank_gain * c->transformer_gain;
    c->predicted_vout   = c->v_in * c->total_gain;
}
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
void printResults(LLC_Converter *c) {
    printf("\n========= LLC Design Results =========\n");
    printf("  Bridge type:              %s\n",
           c->bridge_type == HALF_BRIDGE ? "Half Bridge" : "Full Bridge");
    printf("  Output current (Iout):    %.4f A\n",   c->i_out);
    printf("  Turns ratio (Np:Ns):      %.3f : 1\n", c->turns_ratio);
    printf("  Load resistance (Rload):  %.4f Ohm\n", c->load_resistance);
    printf("  Reflected load (Rac):     %.4f Ohm\n", c->reflected_load);
    printf("  Quality factor (Q):       %.4f\n",     c->quality_factor);
    printf("  Characteristic imp (Zr):  %.4f Ohm\n", c->characteristic_impedance);
    printf("  Resonant freq (fr):       %.3f kHz\n", c->resonant_frequency / 1e3);
    printf("  Resonant inductance (Lr): %.4f uH\n",  c->resonant_inductance * 1e6);
    printf("  Resonant capacitance(Cr): %.4f nF\n",  c->resonant_capacitance * 1e9);
    printf("  Magnetizing inductance (Lm): %.4f uH\n",  c->magnetizing_inductance * 1e6);
    printf("  Lambda (Lm/Lr):           %.4f\n",     c->lambda);
    printf("  Gamma (Lr/Lm):            %.4f\n",     c->inductance_ratio);
    printf("  Normalised freq (fn):     %.4f\n",     c->normalized_frequency);
    printf("--------------------------------------\n");
    printf("  Desired gain:             %.4f\n",     c->desired_gain);
    printf("  Bridge gain:              %.4f\n",     c->bridge_gain);
    printf("  Tank gain:                %.4f\n",     c->tank_gain);
    printf("  Transformer gain:         %.4f\n",     c->transformer_gain);
    printf("  Total gain:               %.4f\n",     c->total_gain);
    printf("  Predicted Vout:           %.4f V\n",   c->predicted_vout);
    printf("  Target Vout:              %.4f V\n",   c->v_out);
    printf("  Gain error:               %.3f%%\n",
           fabs(c->predicted_vout - c->v_out) / c->v_out * 100.0);
    printf("======================================\n");
}
int main(void){
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

    printf("[Q fixed internally at %.2f — reported as output]\n", TARGET_Q);

    calculateLLC(&conv);
    printResults(&conv);

    // Optional gain curve export
    char choice;
    printf("\nExport gain curve to CSV? (y/n): ");
    scanf(" %c", &choice);
    if (choice == 'y' || choice == 'Y')
        exportGainCurve(&conv, "gain_curve.csv");
    return 0;
}