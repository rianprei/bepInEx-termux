#!/usr/bin/env python3
"""Gera jni/patches.h a partir do snapshot de balanceamento lido em runtime
(sa2explore.js -> balance_snapshot.txt). Cada entrada = (categoria, chave,
dado novo do item), aplicada pelo mod via CreateSnapshot + Apply do jogo."""
import json, re, sys, collections

SNAP, GUIDS, OUT = sys.argv[1], sys.argv[2], sys.argv[3]

with open(SNAP) as f:
    SNAP_TXT = f.read()

def load_cat(name):
    m = re.search(r'### CAT %s type=\S+ items=\d+\n(.*?)(?=\n### )' % name, SNAP_TXT, re.S)
    if m is None:
        sys.exit('categoria %s ausente no snapshot %s' % (name, SNAP))
    items = {}
    for it in re.split(r'\n(?=K [^\t\n]+\tD )', m.group(1)):
        if it.startswith('K ') and '\tD ' in it:
            k, d = it[2:].split('\tD ', 1)
            items[k] = d
    return items

with open(GUIDS) as f:
    names = json.load(f)
guid = {v: k for k, v in names.items()}
wep = {k: json.loads(v.split('\n', 1)[0]) for k, v in load_cat('GameBalancePatch_RedneckWeapons').items()}
red = {k: json.loads(v.split('\n', 1)[0]) for k, v in load_cat('GameBalancePatch_Rednecks').items()}
lvl = load_cat('GameBalancePatch_LevelData')
out = []

# 1) Armas primárias cortadas (nenhum personagem usa): cada personagem ganha uma,
#    liberada no nível 1 do personagem.
HIDDEN = {'Slow Joe': 'Kalashnikov', 'Welder': 'ProtonRay', 'Uncle Hairy': 'UziGun',
          'Frosty': 'RayGun', 'Pepper': 'Nailgun', 'Neighbor Willy': 'HamsterGun',
          'Wei': 'M16ElectricGrenadeLauncher', 'Sonny': 'OmeletteDayWeapon',
          'Larry': 'FireExtinguisher'}
for who, wname in HIDDEN.items():
    r = red[who]
    if any(w['w'] == guid[wname] for w in r['w']):
        continue
    r['w'].append({'l': 1, 'd': 0, 'w': guid[wname]})
    out.append(('red', who, json.dumps(r, separators=(',', ':'))))

# 2) Fusões: arma ganha a entrada de dano (efeito) de outra arma.
def entry(src, dtype):
    e = next((e for e in wep[guid[src]]['d'] if e['damageType'] == dtype), None)
    if e is None:
        sys.exit('%s não tem dano tipo %d pra fundir' % (src, dtype))
    return e
FUSIONS = [('Shotgun', 'IceCubeLauncher', 5),      # + congelamento
           ('DoubleShotgun', 'RottenEgg', 3),        # + veneno
           ('Kalashnikov', 'HamsterGun', 4),         # + choque elétrico
           ('TankBusterRifle', 'AtomicBazooka', 9)]  # + radiação
for dst, src, t in FUSIONS:
    w = wep[guid[dst]]
    if not any(e['damageType'] == t for e in w['d']):
        w['d'].append(entry(src, t))
    out.append(('wep', guid[dst], json.dumps(w, separators=(',', ':'))))

# 3) Fases remixadas: L05/L10/L15 dos capítulos 2+ ganham o chefe do capítulo
#    anterior no fim da onda final.
BOSSES = ['Turtle Boss', 'Rat Boss', 'Bear Boss', 'Beaver Boss', 'Monkey Boss', 'Nian Monster',
          'Wolverine Boss', 'Fox Boss', 'Big Alligator']
boss_of = {}
for k, d in lvl.items():
    for b in BOSSES:
        if re.search(r'spawni?\s+"%s"' % re.escape(b), d):
            boss_of.setdefault(k[:3], b)
remix = 0
for k in sorted(lvl):
    m = re.fullmatch(r'C(\d\d)L(05|10|15)', k)
    if not m or int(m.group(1)) < 2:
        continue
    prev = 'C%02d' % (int(m.group(1)) - 1)
    b = boss_of.get(prev)
    if not b or 'finalwave' not in lvl[k]:
        continue
    lines = lvl[k].rstrip('\n').split('\n')
    fw = max(i for i, l in enumerate(lines) if l.strip() == 'finalwave')
    waits = [i for i, l in enumerate(lines) if i > fw and l.strip() == 'wait all']
    add = ['present_screen "%s"' % b, 'spawn "%s" 50%% 3' % b]
    if waits:
        lines[waits[-1]:waits[-1]] = add  # antes do último 'wait all' da onda final
    else:
        lines += add + ['wait all']
    out.append(('ld', k, '\n'.join(lines)))
    remix += 1

# 4) Unknown (card "?" sem corpo, isPlayable 0) vira jogável com o corpo do
#    Slow Joe. O mod copia em memória prefab/skins/melhorias do Slow Joe e
#    aplica este JSON pelo parser do jogo. A arma dele é um clone em runtime
#    da Shotgun (Object.Internal_CloneSingle) com o dano dela + todo efeito.
UNKNOWN_BODY = 'Slow Joe'
un = json.loads(json.dumps(red[UNKNOWN_BODY]))
un['w'] = [w for w in un['w'] if not (w['l'] == 1 and w['d'] == 0)]  # sem a arma escondida do item 1
# Sem fase de liberação (igual Granny/Betty): com "C01L01" herdado do Slow Joe o
# ícone de recompensa do mapa (MapLevelIcon.TrySetupRewardIcon) pegava o
# Unknown e o jogo crashava ao abrir o mapa.
un['a'] = ''
SUPER_BASE = 'Shotgun'
SUPER_EFFECTS = [(1, 'RocketLauncher'),          # explosão
                 (2, 'MolotovSixPack'),          # fogo
                 (3, 'RottenEgg'),               # veneno
                 (4, 'HamsterGun'),              # choque
                 (5, 'IceCubeLauncher'),         # gelo
                 (6, 'IceRifle'),                # lentidão
                 (7, 'DoubleShotgun'),           # atordoamento
                 (9, 'AtomicBazooka'),           # radiação
                 (11, 'SurvivalLevelArtillery')]  # penetração
# MindControl (8) fica de fora: nenhuma arma do jogo tem, sem parâmetro de referência.
sup = json.loads(json.dumps(wep[guid[SUPER_BASE]]))
sup['d'] = [e for e in sup['d'] if e['damageType'] == 0]
for t, src in SUPER_EFFECTS:
    sup['d'].append(entry(src, t))
if un['w'][0]['w'] != guid[SUPER_BASE]:
    sys.exit('arma inicial do %s não é %s' % (UNKNOWN_BODY, SUPER_BASE))
out.append(('red', 'Unknown', json.dumps(un, separators=(',', ':'))))

def c(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
with open(OUT, 'w') as f:
    f.write('// GERADO por tools/gen_patches.py — não editar à mão.\n')
    f.write('struct sa2_patch { const char *cat, *key, *data; };\n')
    f.write('static const sa2_patch SA2_PATCHES[] = {\n')
    for cat, key, data in out:
        f.write('    {%s, %s, %s},\n' % (c(cat), c(key), c(data)))
    f.write('};\n')
    f.write('#define SA2_UNKNOWN_KEY %s\n' % c('Unknown'))
    f.write('#define SA2_UNKNOWN_BODY %s\n' % c(UNKNOWN_BODY))
    f.write('#define SA2_SUPER_BASE_GUID %s\n' % c(guid[SUPER_BASE]))
    f.write('#define SA2_SUPER_WEP %s\n' % c(json.dumps(sup, separators=(',', ':'))))
print('super arma: %s + efeitos %s' % (SUPER_BASE, [t for t, _ in SUPER_EFFECTS]))
print('patches: red=%d wep=%d ld=%d (chefes por capítulo: %s)' % (
    sum(1 for o in out if o[0] == 'red'), sum(1 for o in out if o[0] == 'wep'), remix, dict(sorted(boss_of.items()))))
