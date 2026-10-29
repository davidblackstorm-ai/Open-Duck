"""Plot measured output from the explicitly installed reference VST3."""
from pathlib import Path
import csv
import sys
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'work/pythonlibs'))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

work=root/'work/revision041/Reference'
def gain(folder):
    with (work/folder/'reference-output.csv').open(newline='') as stream:
        data=list(csv.DictReader(stream))
    return np.array([float(row['sample'])/48 for row in data]),np.array([float(row['output'])/.15*100 for row in data])
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.facecolor':'#0c1d28','figure.facecolor':'#07121c','axes.edgecolor':'#355066','text.color':'#e4eff8','axes.labelcolor':'#adc4d4','xtick.color':'#adc4d4','ytick.color':'#adc4d4','grid.color':'#29414f','grid.alpha':.6})
fig,axes=plt.subplots(2,2,figsize=(13,8),layout='constrained')
fig.suptitle('Kickstart 2 · misure funzionali sul plugin installato',fontsize=18,fontweight='bold')
for amount,color in zip([0,25,50,75,100],['#d9e3ed','#77bdce','#45d0e1','#12e4ed','#00fcdd']):
    time,values=gain(f'Mix-{amount}');axes[0,0].plot(time,values,label=f'MIX {amount}%',color=color,lw=1.6)
axes[0,0].set(title='MIX: profondità lineare, posizione invariata',xlabel='Tempo nel ciclo [ms]',ylabel='Guadagno [%]');axes[0,0].legend(frameon=False,labelcolor='#d4e4ef',fontsize=8)
for index in [1,2,3,4]:
    time,values=gain(f'Wave-{index:02}');axes[0,1].plot(time,values,label=f'Curva {index}',lw=1.5)
axes[0,1].set(title='Profili reali acquisiti — primi quattro',xlabel='Tempo nel ciclo [ms]',ylabel='Guadagno [%]');axes[0,1].legend(frameon=False,labelcolor='#d4e4ef',fontsize=8)
time,values=gain('Wave-11');axes[1,0].plot(time,values,color='#26deed',lw=1.5);axes[1,0].set(title='Curva 11 — uscita misurata',xlabel='Tempo nel ciclo [ms]',ylabel='Guadagno [%]')
mask=time<.4;axes[1,1].plot(time[mask],values[mask],color='#26deed',marker='.',lw=1.2);axes[1,1].axhline(100,color='#90aabc',lw=.8,ls='--');axes[1,1].set(title='Dettaglio del bordo: confronto numerico necessario',xlabel='Tempo nel ciclo [ms]',ylabel='Guadagno [%]')
for axis in axes.flat:axis.grid(True);axis.set_axisbelow(True)
fig.supxlabel('Banco indipendente · 48 kHz · 120 BPM · ingresso costante 0,15 · nessun dispositivo audio aperto',fontsize=9,color='#9bb9cc')
outputs=Path(r'C:\Users\PC\Documents\Codex\2026-10-29\recupera-quello-che-stavamo-facendo-per\outputs');outputs.mkdir(parents=True,exist_ok=True)
for destination in [work/'misure-funzionali.png',outputs/'Misure-Kickstart-2-funzionali.png']:
    fig.savefig(destination,dpi=150)
print('Measured-reference plot exported; no product UI or waveform invented.')
