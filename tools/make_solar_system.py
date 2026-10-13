#!/usr/bin/env python3
"""Generates physics/models/solar_system.xml and solar_system_asteroids.xml from NASA/JPL data.

  python make_solar_system.py ..\\physics\\models [cache_dir]

Needs network access to JPL Horizons (state vectors, satellite masses and radii) and the JPL Small-Body
Database (asteroid sizes and masses). Responses are cached in cache_dir so a rerun is quick.

Both models hold the Sun, the eight planets, Pluto and every natural satellite Horizons has an
ephemeris for. The asteroids model adds the ten most massive asteroids and the next 250 largest
main-belt asteroids. The simulator is 2D, so each orbit is laid flat into the ecliptic plane: a body is
placed relative to its parent with the same distance, speed and position along its orbit, with the orbit
plane rotated onto the ecliptic about the line of nodes (the sense of rotation, prograde or retrograde,
is kept). The system is then shifted to its centre of mass.
"""
import concurrent.futures
import json
import math
import os
import re
import sys
import tempfile
import time
import urllib.parse
import urllib.request

G = 6.6743e-11
EPOCH_START, EPOCH_STOP = "2026-10-10", "2026-10-11"
EPOCH_TEXT = "2026-10-10 00:00 TDB"
HORIZONS = "https://ssd.jpl.nasa.gov/api/horizons.api"
SBDB_QUERY = "https://ssd-api.jpl.nasa.gov/sbdb_query.api"

# name, Horizons id, parent id (None for the Sun), mass kg, radius m, colour. Values from the original model.
CORE = [
    ('Sun', 10, None, 1.9884098713264315E+30, 695700000, '#FFD700'),
    ('Mercury', 199, 10, 3.3010006369207261E+23, 2439700, '#A9A9A9'),
    ('Venus', 299, 10, 4.8673058148420064E+24, 6051800, '#E8C170'),
    ('Earth', 399, 10, 5.9721683987234617E+24, 6371000, '#3C7DFF'),
    ('Moon', 301, 399, 7.3457891703998927E+22, 1737400, '#D0D0D0'),
    ('Mars', 499, 10, 6.4169089213850146E+23, 3389500, '#D1492E'),
    ('Phobos', 401, 499, 1.0659E+16, 11100, '#8B7765'),
    ('Deimos', 402, 499, 1.4762E+15, 6200, '#A39382'),
    ('Jupiter', 599, 10, 1.8981246258034552E+27, 69911000, '#D9A066'),
    ('Io', 501, 599, 8.9296495512638034E+22, 1821600, '#F2E04C'),
    ('Europa', 502, 599, 4.7986140868705335E+22, 1560800, '#E0D2B4'),
    ('Ganymede', 503, 599, 1.4814788067662528E+23, 2634100, '#A89F91'),
    ('Callisto', 504, 599, 1.0756608183629745E+23, 2410300, '#6E655A'),
    ('Saturn', 699, 10, 5.6831737012121129E+26, 58232000, '#EAD6A2'),
    ('Mimas', 601, 699, 3.7493E+19, 198000, '#C8C8C8'),
    ('Enceladus', 602, 699, 1.08022E+20, 252000, '#FFFFFF'),
    ('Tethys', 603, 699, 6.1745E+20, 531000, '#E6E6E6'),
    ('Dione', 604, 699, 1.095452E+21, 561000, '#DADADA'),
    ('Rhea', 605, 699, 2.306518E+21, 764000, '#CFCFCF'),
    ('Titan', 606, 699, 1.3451807680206165E+23, 2574700, '#E3A33B'),
    ('Iapetus', 608, 699, 1.805635E+21, 734000, '#B8AE9C'),
    ('Uranus', 799, 10, 8.6809871537090045E+25, 25362000, '#7DE3E8'),
    ('Miranda', 705, 799, 6.4E+19, 236000, '#BFBFBF'),
    ('Ariel', 701, 799, 1.251E+21, 579000, '#D3D3D3'),
    ('Umbriel', 702, 799, 1.275E+21, 585000, '#8F8F8F'),
    ('Titania', 703, 799, 3.4E+21, 789000, '#C9B8A8'),
    ('Oberon', 704, 799, 3.076E+21, 761000, '#A89888'),
    ('Neptune', 899, 10, 1.0240924096909039E+26, 24622000, '#3F54BA'),
    ('Triton', 801, 899, 2.1389479046491767E+22, 1353400, '#D7C4C4'),
    ('Proteus', 808, 899, 4.4E+19, 210000, '#888888'),
]
PLUTO = ('Pluto', 999, 10, None, None, '#C8B5A0')
CHARON_COLOUR = '#8A8A8A'

# The ten most massive asteroids (published estimates; ranks 5 to 10 are close and uncertain).
# spkid, mass in kg when the Small-Body Database has no GM for it.
TOP_ASTEROIDS = [
    (20000001, None), (20000004, None), (20000002, None), (20000010, None), (20000704, None),
    (20000511, 3.4e19), (20000015, 3.1e19), (20000003, 2.7e19), (20000016, None), (20000052, 2.4e19),
]
EXTRA_ASTEROIDS = 250

DEFAULT_MOON_RADIUS_M = 2000.0
DEFAULT_MOON_DENSITY = 1500.0
ASTEROID_COLOUR = '#383838'
MINOR_MOON_COLOUR = '#383838'


def fetch(url, cache, key):
    path = os.path.join(cache, re.sub(r'[^A-Za-z0-9_.=-]', '_', key) + '.txt')
    if os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            return f.read()
    for attempt in range(6):
        try:
            with urllib.request.urlopen(url, timeout=90) as response:
                text = response.read().decode('utf-8', 'replace')
            with open(path, 'w', encoding='utf-8') as f:
                f.write(text)
            return text
        except Exception as error:
            if attempt == 5:
                raise
            time.sleep(2 + 3 * attempt)


def horizons(command, centre, cache, obj_data=True):
    query = {
        'format': 'text', 'COMMAND': "'%s'" % command, 'OBJ_DATA': "'YES'" if obj_data else "'NO'",
        'MAKE_EPHEM': "'YES'", 'EPHEM_TYPE': "'VECTORS'", 'CENTER': "'%s'" % centre,
        'START_TIME': "'%s'" % EPOCH_START, 'STOP_TIME': "'%s'" % EPOCH_STOP, 'STEP_SIZE': "'1 d'",
        'REF_PLANE': "'ECLIPTIC'", 'REF_SYSTEM': "'ICRF'", 'OUT_UNITS': "'KM-S'", 'VEC_TABLE': "'2'", 'CSV_FORMAT': "'YES'",
    }
    url = HORIZONS + '?' + urllib.parse.urlencode(query, quote_via=urllib.parse.quote)
    return fetch(url, cache, 'hz_%s_%s' % (command, centre))


def parse_vector(text):
    """Position (m) and velocity (m/s) at the first epoch."""
    i = text.find('$$SOE')
    if i < 0:
        return None
    fields = [f.strip() for f in text[i:].split('\n')[1].split(',')]
    values = [float(v) for v in fields[2:8]]
    return [v * 1000.0 for v in values[:3]], [v * 1000.0 for v in values[3:]]


NUMBER = r'([0-9]*\.?[0-9]+(?:[eE][+-]?\d+)?)'


def parse_physical(text):
    """(GM in km^3/s^2 or None, mass in kg or None, radius in km or None) from the object data block."""
    head = text.split('$$SOE')[0]
    gm = re.search(r'\bGM\s*(?:\(\s*km\^?3\s*/\s*s\^?2\s*\))?\s*[=:]\s*' + NUMBER, head) or re.search(r'\bGM=\s*' + NUMBER, head)
    mass = re.search(r'Mass\s*(?:x\s*)?\(?\s*10\^(\d+)\s*\(?\s*kg\s*\)?\s*\)?\s*=\s*' + NUMBER, head)
    radius = (re.search(r'(?:Mean radius|Vol\.? mean radius|Radius)\s*\([^)]*km[^)]*\)\s*=\s*' + NUMBER, head)
              or re.search(r'\bRAD=\s*' + NUMBER, head))
    return (float(gm.group(1)) if gm else None,
            float(mass.group(2)) * 10.0 ** int(mass.group(1)) if mass else None,
            float(radius.group(1)) if radius else None)


def satellite_parent(identifier):
    for low, high, parent in ((401, 499, 499), (501, 599, 599), (601, 699, 699), (701, 799, 799), (801, 899, 899), (901, 999, 999),
                              (55000, 55999, 599), (65000, 65999, 699), (75000, 75999, 799), (85000, 85999, 899)):
        if low <= identifier < high:
            return parent
    return None


def moon_label(name):
    m = re.match(r'^S(\d{4})_([A-Z])(\d+)$', name)
    if m:
        return 'S/%s %s %s' % m.groups()
    return name.replace('_', ' ')


def list_moons(cache):
    text = fetch(HORIZONS + '?format=text&COMMAND=%27MB%27', cache, 'hz_MB')
    moons = []
    for line in text.split('\n'):
        m = re.match(r'^\s+(\d{3,5})\s+(.+?)\s{2,}', line)
        if not m:
            continue
        identifier, name = int(m.group(1)), m.group(2).strip()
        parent = satellite_parent(identifier)
        if parent is not None and not name.lower().startswith('barycenter'):
            moons.append((identifier, name, parent))
    return moons


def flatten(r, v):
    """Rotate the orbit plane onto the ecliptic (about the line of nodes), keeping the sense of rotation."""
    h = (r[1] * v[2] - r[2] * v[1], r[2] * v[0] - r[0] * v[2], r[0] * v[1] - r[1] * v[0])
    hn = math.sqrt(sum(c * c for c in h))
    if hn == 0:
        return r, v
    h = tuple(c / hn for c in h)
    target = 1.0 if h[2] >= 0 else -1.0
    axis = (h[1] * target, -h[0] * target, 0.0)   # h x (0, 0, target)
    sin = math.sqrt(axis[0] ** 2 + axis[1] ** 2)
    cos = h[2] * target
    if sin < 1e-12:
        return r, v
    k = (axis[0] / sin, axis[1] / sin, 0.0)

    def rotate(a):
        cross = (k[1] * a[2] - k[2] * a[1], k[2] * a[0] - k[0] * a[2], k[0] * a[1] - k[1] * a[0])
        dot = k[0] * a[0] + k[1] * a[1]
        return [a[i] * cos + cross[i] * sin + k[i] * dot * (1.0 - cos) for i in range(3)]

    return rotate(r), rotate(v)


def density_for(taxonomy):
    t = (taxonomy or '').strip().upper()[:1]
    if t in ('C', 'B', 'F', 'G', 'P', 'D', 'T'):
        return 1700.0
    if t in ('S', 'Q', 'A', 'R', 'L', 'K', 'O'):
        return 2700.0
    if t in ('M', 'X', 'E'):
        return 3500.0
    if t == 'V':
        return 3400.0
    return 2000.0


def sbdb(cache, key, **params):
    url = SBDB_QUERY + '?' + urllib.parse.urlencode(params, quote_via=urllib.parse.quote)
    return json.loads(fetch(url, cache, 'sbdb_' + key))


def asteroid_table(cache):
    fields = 'spkid,name,pdes,diameter,GM,spec_B,spec_T'
    with_gm = sbdb(cache, 'gm', fields=fields, **{'sb-kind': 'a', 'sb-cdata': '{"AND":["GM|DF"]}'})
    by_id = {int(row[0]): row for row in with_gm['data']}
    largest = sbdb(cache, 'largest', fields=fields, **{'sb-kind': 'a', 'sb-class': 'IMB,MBA,OMB,MCA', 'sb-cdata': '{"AND":["diameter|RG|60|2000"]}', 'sort': '-diameter', 'limit': '600'})
    sized = {int(row[0]): row for row in largest['data']}
    return by_id, sized


def asteroid_body(row, mass_override):
    spkid, name, pdes, diameter, gm, spec_b, spec_t = row
    label = (name or pdes).strip()
    radius = float(diameter) * 500.0 if diameter else 50000.0
    if gm:
        mass = float(gm) * 1e9 / G
    elif mass_override:
        mass = mass_override
    else:
        mass = density_for(spec_b or spec_t) * 4.0 / 3.0 * math.pi * radius ** 3
    return label, mass, radius


def build(models_dir, cache):
    os.makedirs(cache, exist_ok=True)
    moons = [m for m in list_moons(cache) if m[0] not in {c[1] for c in CORE}]
    by_gm, sized = asteroid_table(cache)

    jobs = {}   # (command, centre) -> text
    wanted = [(str(c[1]), '500@10' if c[2] == 10 else '500@%d' % c[2]) for c in CORE[1:]]
    wanted.append((str(PLUTO[1]), '500@10'))
    wanted += [(str(i), '500@%d' % p) for i, _, p in moons]
    top_ids = [t[0] for t in TOP_ASTEROIDS]
    extra_ids = [i for i in sized if i not in top_ids][:EXTRA_ASTEROIDS]
    wanted += [('DES=%d' % i, '500@10') for i in top_ids + extra_ids]

    def work(item):
        return item, horizons(item[0], item[1], cache)

    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        for n, (item, text) in enumerate(pool.map(work, wanted), 1):
            jobs[item] = text
            if n % 50 == 0:
                print('fetched %d / %d' % (n, len(wanted)), flush=True)

    def vector(command, centre):
        parsed = parse_vector(jobs[(command, centre)])
        if parsed is None:
            raise RuntimeError('no ephemeris for %s relative to %s' % (command, centre))
        return parsed

    def flat_state(command, centre, parent_state):
        r, v = flatten(*vector(command, centre))
        return ([parent_state[0][0] + r[0], parent_state[0][1] + r[1]], [parent_state[1][0] + v[0], parent_state[1][1] + v[1]])

    bodies = []   # dicts: label, m, r, colour, x, y, vx, vy, group
    states = {10: ([0.0, 0.0], [0.0, 0.0])}
    sun = CORE[0]
    bodies.append(dict(label=sun[0], m=sun[3], r=sun[4], colour=sun[5], state=states[10], group=None))
    for name, identifier, parent, mass, radius, colour in CORE[1:]:
        state = flat_state(str(identifier), '500@10' if parent == 10 else '500@%d' % parent, states[parent])
        states[identifier] = state
        bodies.append(dict(label=name, m=mass, r=radius, colour=colour, state=state, group=None))

    skipped = []

    def physical(text, fallback_radius_m=DEFAULT_MOON_RADIUS_M):
        gm, mass, radius_km = parse_physical(text)
        radius = radius_km * 1000.0 if radius_km else fallback_radius_m
        if gm and gm > 0:
            return gm * 1e9 / G, radius, True
        if mass and mass > 0:
            return mass, radius, True
        return DEFAULT_MOON_DENSITY * 4.0 / 3.0 * math.pi * radius ** 3, radius, False

    pluto_text = jobs[(str(PLUTO[1]), '500@10')]
    pluto_mass, pluto_radius, _ = physical(pluto_text, 1.19e6)
    states[999] = flat_state('999', '500@10', states[10])
    bodies.append(dict(label='Pluto', m=pluto_mass, r=pluto_radius, colour=PLUTO[5], state=states[999], group='Pluto and its moons'))

    estimated = 0
    for identifier, name, parent in sorted(moons, key=lambda m: ({999: 0, 499: 1, 599: 2, 699: 3, 799: 4, 899: 5}[m[2]], m[0])):
        text = jobs[(str(identifier), '500@%d' % parent)]
        if parse_vector(text) is None:
            skipped.append(name)
            continue
        mass, radius, known = physical(text)
        estimated += 0 if known else 1
        group = {999: 'Pluto and its moons', 499: 'Other moons of Mars', 599: 'Other moons of Jupiter', 699: 'Other moons of Saturn',
                 799: 'Other moons of Uranus', 899: 'Other moons of Neptune'}[parent]
        colour = CHARON_COLOUR if name == 'Charon' else MINOR_MOON_COLOUR
        bodies.append(dict(label=moon_label(name), m=mass, r=radius, colour=colour, state=flat_state(str(identifier), '500@%d' % parent, states[parent]), group=group))

    core_count = len(bodies)
    asteroid_bodies = []
    for rank, spkid in enumerate(top_ids + extra_ids):
        row = sized.get(spkid) or by_gm.get(spkid)
        if spkid in by_gm:
            row = by_gm[spkid]
            if sized.get(spkid) and not row[3]:
                row = list(row)
                row[3] = sized[spkid][3]
        override = dict(TOP_ASTEROIDS).get(spkid)
        label, mass, radius = asteroid_body(row, override)
        group = 'The ten most massive asteroids' if rank == 0 else ('The next largest main-belt asteroids' if rank == len(top_ids) else None)
        asteroid_bodies.append(dict(label=label, m=mass, r=radius, colour=ASTEROID_COLOUR, state=flat_state('DES=%d' % spkid, '500@10', states[10]), group=group))

    print('moons: %d, estimated mass for %d, skipped %d %s' % (len(moons), estimated, len(skipped), skipped))
    print('asteroids: %d' % len(asteroid_bodies))

    moon_count = len(bodies) - 10   # everything except the Sun, the eight planets and Pluto
    write_model(os.path.join(models_dir, 'solar_system.xml'), bodies, moon_count, 0)
    write_model(os.path.join(models_dir, 'solar_system_asteroids.xml'), bodies + asteroid_bodies, moon_count, len(asteroid_bodies))


def number(x):
    return repr(float(x))


def escape(text):
    return text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


def write_model(path, bodies, moon_count, asteroid_count):
    total = sum(b['m'] for b in bodies)
    cx = sum(b['m'] * b['state'][0][0] for b in bodies) / total
    cy = sum(b['m'] * b['state'][0][1] for b in bodies) / total
    cvx = sum(b['m'] * b['state'][1][0] for b in bodies) / total
    cvy = sum(b['m'] * b['state'][1][1] for b in bodies) / total

    lines = ['<?xml version="1.0" encoding="utf-8"?>', '<!--']
    lines.append('  The Sun, the eight planets, Pluto and %d moons on %s%s.' % (moon_count, EPOCH_TEXT, ', with %d main-belt asteroids' % asteroid_count if asteroid_count else ''))
    lines.append('  Positions and velocities come from NASA/JPL Horizons (DE441 and satellite ephemerides) in the')
    lines.append('  ecliptic frame. The simulator is 2D, so each orbit is laid flat into the ecliptic plane: a planet is')
    lines.append('  placed relative to the Sun, and a moon relative to its planet, with the same distance, speed and')
    lines.append('  position along its orbit but with the orbit tilted into the plane. The whole system is shifted to its')
    lines.append('  centre of mass so it does not drift. Masses use GM values (G = 6.6743e-11) so orbits close properly.')
    lines.append('  Moons without a published mass or radius use a radius of 2 km and a density of 1500 kg/m^3.')
    lines.append('  Daphnis is left out: Horizons has no ephemeris for it after 2018.')
    if asteroid_count:
        lines.append('  The ten most massive asteroids (Ceres, Vesta, Pallas, Hygiea, Interamnia, Davida, Eunomia, Juno, Psyche')
        lines.append('  and Europa) use published masses; the other %d are the next largest main-belt asteroids, with masses' % (asteroid_count - 10))
        lines.append('  from their diameter and an assumed density for their type where no GM is known.')
    lines.append('  Units are metres, kilograms and seconds; dt = 100 s and 100 steps per frame, about 7 days per second at 60 fps.')
    lines.append("  Radii are real, so most bodies are far smaller than a pixel unless you zoom in. Saturn's rings are not modelled.")
    lines.append('-->')
    lines.append('<simulation xmin="-7e12" xmax="7e12" ymin="-7e12" ymax="7e12" dt="100" iterations="100" bounce_bottom="false">')
    lines.append('  <masses>')
    previous_group = None
    for index, b in enumerate(bodies):
        if b['group'] and b['group'] != previous_group:
            lines.append('    <!-- %s -->' % b['group'])
        previous_group = b['group'] or previous_group
        if index < len(CORE):
            lines.append('    <!-- %s -->' % b['label'])
        (x, y), (vx, vy) = b['state']
        attrs = 'm="%s" r="%s" x="%s" y="%s" vx="%s" vy="%s"' % (number(b['m']), number(b['r']), number(x - cx), number(y - cy), number(vx - cvx), number(vy - cvy))
        if b['colour']:
            attrs += ' colour="%s"' % b['colour']
        attrs += ' label="%s"' % escape(b['label'])
        lines.append('    <mass %s/>' % attrs)
    lines.append('  </masses>')
    lines.append('</simulation>')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('wrote %s (%d bodies)' % (path, len(bodies)))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    build(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else os.path.join(tempfile.gettempdir(), 'solar_system_cache'))
