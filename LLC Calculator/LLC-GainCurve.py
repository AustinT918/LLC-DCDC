import numpy as np
import matplotlib.pyplot as plt

# Design Specs
Vin_max = 125
Vin_nom = 108
Vin_min = 75

Vout = 13.4

Iout_max = 30.1

# Transformer Turns
n = (1/2) * Vin_nom/Vout
print("Turn Ratio:", n)

# Values (refer to LLC-Value)
Lr = 3.943e-06
Cr = 2.855e-07
Lm = 8.220e-06

fsw = 150e3
fr = 1/(2*np.pi*np.sqrt(Lr*Cr))
print("Resonant Frequency:", fr)

fn = fsw/fr
Lratio = Lr/Lm
print("Lratio:", Lratio)

Z = np.sqrt(Lr/Cr)

# Vout/Vin = 1/2n * M

# Min and Max Gains
M_max = 2*n*Vout/Vin_min
M_min = 2*n*Vout/Vin_max
print(M_max)
print(M_min)

# Gain Curve
Iout_values = {
    'Full Load':   Iout_max,
    'Half Load':   Iout_max / 2,
    'Quarter Load': Iout_max / 4,
    'No Load':     Iout_max / 20,  # near-zero, avoid division by zero
}

colors = ['steelblue', 'seagreen', 'darkorange', 'mediumpurple']

fn_range = np.linspace(0.3, 1.5, 1000)

plt.figure(figsize=(10, 6))

for (label, Iout), color in zip(Iout_values.items(), colors):
    R_i   = Vout / Iout
    Re_i  = 8 * R_i * n**2 / np.pi**2
    Q_i   = Z / Re_i
    M_i   = 1/np.sqrt((1+Lratio-(Lratio/fn_range**2))**2 + Q_i**2*(fn_range - 1/fn_range)**2)
    plt.plot(fn_range, M_i, label=f'{label}  (Q={Q_i:.3f})', color=color)

plt.axhline(M_max, color='red',   linestyle='--', linewidth=1.2, label=f'M_max = {M_max:.3f}')
plt.axhline(M_min, color='black', linestyle='--', linewidth=1.2, label=f'M_min = {M_min:.3f}')
plt.axvline(1.0,   color='gray',  linestyle=':',  linewidth=1.0, label='fn = 1 (resonance)')

plt.xlabel('fn  (fsw / fr)')
plt.ylabel('Voltage Gain M')
plt.title('LLC Resonant Converter Gain Curves vs Load')
plt.legend(fontsize=8)
plt.grid(True, alpha=0.4)
plt.ylim(0, 2)
plt.xlim(0.3, 1.5)
plt.tight_layout()
plt.show()
