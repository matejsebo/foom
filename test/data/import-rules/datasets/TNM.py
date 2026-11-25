# U.S. Geological Survey, The National Map (vector data): FOOM's mapping to ISOM 2017-2 and ISSprOM 2019.
# Read by ../make_tables.py. Copyright 2026 Matej Šebo, GPL-3 or later (see make_tables.py).

TITLE = 'U.S. Geological Survey, The National Map (hydrography, transportation, structures, contours)'

HEADER = [
    'Catalogue: the layers, fields, subtypes and coded values which the services of The National Map',
    'publish, read on 2026-11-24:',
    'https://hydro.nationalmap.gov/arcgis/rest/services/nhd/MapServer (National Hydrography Dataset),',
    'https://carto.nationalmap.gov/arcgis/rest/services/transportation/MapServer (National',
    'Transportation Dataset), .../structures/MapServer (National Structures Dataset) and',
    '.../contours/MapServer (contours of the 3D Elevation Program).',
    'Data licence: the data are works of the U.S. Government and in the public domain.',
    '',
    'The classes are numbers and words in the fields FType and FCode (hydrography, railways, runways,',
    'structures, contours), TNMFRC (roads) and TrailType (trails). One hydrography class is a line in',
    'one layer and an area or a point in another, so its rules test the layer name too: import with',
    'Layer name tag: Layer name. The layer names are those of the downloads of The National Map',
    '(NHDFlowline, NHDWaterbody, NHDArea, NHDPoint, NHDLine, Trans_RoadSegment, Trans_TrailSegment,',
    'Trans_RailFeature, Trans_AirportRunway, Struct_Point, Elev_Contour), in that spelling or in',
    'lower case; every field is accepted as the services spell it (lower case), in upper case and',
    'in mixed case. The names in a downloaded file were not checked; a Shapefile cuts field names',
    'to ten letters, which the rule of the wide trails (OHVOver50Inches) does not follow.',
    '',
    'Scale: hydrography and roads are compiled at 1:24,000 or smaller; trails come from the land',
    'managers. The contours are in feet at intervals of 10 to 80 feet. Use the data as a base for',
    'field work, not as a map; OpenStreetMap often has more of the trails, and contours in metres',
    'come from an elevation model (the new map wizard fetches one from the USGS).',
]

NO_SYMBOL = 'no orienteering symbol'
SITE = 'points of institutions and facilities; their buildings are not in the data'
UNDER = 'under ground or under water'
NETWORK = 'lines of the network, not features on the ground'
ZONE = 'zones and plans, not features on the ground'
DAM = 'dams and weirs; of earth or of concrete is not said'

SPELLINGS = {
    'ftype': ('FType', 'ftype', 'FTYPE'),
    'fcode': ('FCode', 'fcode', 'FCODE'),
    'tnmfrc': ('TNMFRC', 'tnmfrc'),
    'trailtype': ('TrailType', 'trailtype', 'TRAILTYPE'),
    'ohvover50inches': ('OHVOver50Inches', 'ohvover50inches', 'OHVOVER50INCHES'),
}


def quote(value):
    text = str(value)
    return text if text.replace('_', '').isalnum() else '"%s"' % text


def field(name, value):
    return '(' + ' OR '.join('%s = %s' % (spelling, quote(value)) for spelling in SPELLINGS[name]) + ')'


def in_layer(fragment):
    return '(layer_name ~= %s OR layer_name ~= %s)' % (fragment, fragment.lower())


def c(layer, geom, isom, spr, why=None, fragment=None, **attrs):
    terms = [in_layer(fragment)] if fragment else []
    terms += [field(name, value) for name, value in attrs.items()]
    d = {'layer': layer, 'attrs': attrs, 'geom': geom, 'isom': isom, 'spr': spr, 'q': ' AND '.join(terms)}
    if why:
        d['why'] = why
    return d


def flowline(isom, spr, why=None, **attrs):
    return c('NHDFlowline', 'L', isom, spr, why, 'NHDFlowline', **attrs)


def waterbody(ftype, isom, spr, why=None):
    return c('NHDWaterbody', 'A', isom, spr, why, 'NHDWaterbody', ftype=ftype)


def area(ftype, isom, spr, why=None):
    return c('NHDArea', 'A', isom, spr, why, 'NHDArea', ftype=ftype)


def point(ftype, isom, spr, why=None):
    return c('NHDPoint', 'P', isom, spr, why, 'NHDPoint', ftype=ftype)


def line(ftype, isom, spr, why=None):
    return c('NHDLine', 'L', isom, spr, why, 'NHDLine', ftype=ftype)


CLASSES = [
    # National Hydrography Dataset, NHDFlowline: the subtypes of FType; the streams by FCode.
    flowline('305', '305', ftype=460),                     # StreamRiver: perennial (46006) and unspecified (46000)
    flowline('306', '306', ftype=460, fcode=46003),        # intermittent
    flowline('306', '306', ftype=460, fcode=46007),        # ephemeral
    flowline('305', '305', ftype=336),                     # CanalDitch
    flowline('306', '306', ftype=468),                     # Drainageway
    flowline(None, None, why=NETWORK, ftype=558),          # ArtificialPath, the centre lines of water areas
    flowline(None, None, why=NETWORK, ftype=334),          # Connector
    flowline(None, None, why=NETWORK, ftype=566),          # Coastline, the edge of the sea area
    flowline(None, None, why=UNDER, ftype=420),            # Underground Conduit
    flowline(None, None, why='pipelines; above or under ground is not said', ftype=428),
    # NHDWaterbody
    waterbody(390, '301.1', '301.1'),                      # LakePond
    waterbody(436, '301.1', '301.1'),                      # Reservoir
    waterbody(493, '301.1', '301.1'),                      # Estuary
    waterbody(466, '308', '308'),                          # SwampMarsh
    waterbody(361, None, None, why='dry lake beds'),       # Playa
    waterbody(378, None, None, why=NO_SYMBOL),             # Ice Mass
    # NHDArea
    area(460, '301.1', '301.1'),                           # StreamRiver
    area(336, '301.1', '301.1'),                           # CanalDitch
    area(445, '301.1', '301.1'),                           # SeaOcean
    area(312, '301.1', '301.1'),                           # BayInlet
    area(431, '301.1', '301.1'),                           # Rapids
    area(398, '301.1', '301.1'),                           # Lock Chamber
    area(403, '310', '310'),                               # Inundation Area
    area(537, '310', '310'),                               # Area of Complex Channels
    area(484, '213', '213'),                               # Wash
    area(364, '213', '213'),                               # Foreshore
    area(343, None, None, why=DAM),                        # DamWeir
    area(568, None, None, why=DAM),                        # Levee
    area(362, None, None, why=NO_SYMBOL),                  # Flume
    area(455, None, None, why=NO_SYMBOL),                  # Spillway
    area(485, None, None, why=NO_SYMBOL),                  # Water IntakeOutflow
    area(318, None, None, why='bridges; the roads and paths which cross are in their own layers'),
    area(307, None, None, why=ZONE),                       # Area to be Submerged
    area(373, None, None, why=ZONE),                       # Hazard Zone
    area(454, None, None, why=ZONE),                       # Special Use Zone
    area(461, None, None, why=UNDER),                      # Submerged Stream
    # NHDPoint
    point(458, '312', '312'),                              # SpringSeep
    point(488, '311', '311'),                              # Well
    point(436, '311', '311'),                              # Reservoir as a point: a tank or a trough
    point(487, '313', '313'),                              # Waterfall
    point(441, '204', '204'),                              # Rock
    point(450, None, None, why=NO_SYMBOL),                 # SinkRise
    point(431, None, None, why=NO_SYMBOL),                 # Rapids
    point(367, None, None, why=NO_SYMBOL),                 # Gaging Station
    point(343, None, None, why=DAM),                       # DamWeir
    point(398, None, None, why=NO_SYMBOL),                 # Lock Chamber
    point(485, None, None, why=NO_SYMBOL),                 # Water IntakeOutflow
    point(369, None, None, why=NO_SYMBOL),                 # Gate
    # NHDLine
    line(568, '105', '105'),                               # Levee
    line(483, '513', '513.1'),                             # Wall
    line(343, None, None, why=DAM),                        # DamWeir
    line(487, None, None, why=NO_SYMBOL),                  # Waterfall
    line(431, None, None, why=NO_SYMBOL),                  # Rapids
    line(450, None, None, why=NO_SYMBOL),                  # SinkRise
    line(434, None, None, why=UNDER),                      # Reef
    line(503, None, None, why=ZONE),                       # Sounding Datum Line
    line(369, None, None, why=NO_SYMBOL),                  # Gate
    line(398, None, None, why=NO_SYMBOL),                  # Lock Chamber
    line(478, None, None, why=UNDER),                      # Tunnel
    line(318, None, None, why='bridges; the roads and paths which cross are in their own layers'),
    line(411, None, None, why=NO_SYMBOL),                  # Nonearthen Shore
    line(362, None, None, why=NO_SYMBOL),                  # Flume
    # National Transportation Dataset, Trans_RoadSegment: the road class TNMFRC.
    c('Trans_RoadSegment', 'L', '502', '501.19', tnmfrc=1),    # Controlled-access Highway
    c('Trans_RoadSegment', 'L', '502', '501.19', tnmfrc=2),    # Secondary Highway or Major Connecting Road
    c('Trans_RoadSegment', 'L', '503', '501.8', tnmfrc=3),     # Local Connecting Road
    c('Trans_RoadSegment', 'L', '503', '501.8', tnmfrc=4),     # Local Road
    c('Trans_RoadSegment', 'L', '502', '501.19', tnmfrc=5),    # Ramp
    c('Trans_RoadSegment', 'L', '504', '505.2', tnmfrc=6),     # 4WD
    c('Trans_RoadSegment', 'L', None, None, why='ferry routes', tnmfrc=7),
    c('Trans_RoadSegment', 'L', None, None, why='roads in tunnels are not drawn', tnmfrc=8),
    c('Trans_RoadSegment', 'L', '504', '505.2', tnmfrc=9),     # Closed Road
    c('Trans_RoadSegment', 'L', '503', '501.8', tnmfrc=99),    # Unknown
    # Trans_TrailSegment: the trail type; a trail open to vehicles wider than 50 inches is a track.
    c('Trans_TrailSegment', 'L', '505', '505.1', trailtype='Terra Trail'),
    c('Trans_TrailSegment', 'L', '504', '505.2', trailtype='Terra Trail', ohvover50inches='Y'),
    c('Trans_TrailSegment', 'L', None, None, why='routes on snow', trailtype='Snow Trail'),
    c('Trans_TrailSegment', 'L', None, None, why='routes on water', trailtype='Water Trail'),
    # Trans_RailFeature
    c('Trans_RailFeature', 'L', '509', '509.1', fcode=20200),  # Rail
    c('Trans_RailFeature', 'L', '509', '509.1', fcode=20201),  # Rail: Siding/Spur
    c('Trans_RailFeature', 'L', '509', '509.1', fcode=20205),  # Narrow Gauge
    c('Trans_RailFeature', 'L', '509', '509.2', fcode=20203),  # Light Rail
    c('Trans_RailFeature', 'L', '509', '509.2', fcode=20204),  # Street Car
    c('Trans_RailFeature', 'L', None, None, why=UNDER, fcode=20202),   # Subway
    c('Trans_RailFeature', 'L', None, None, why=UNDER, fcode=20206),   # Railroad Tunnel
    # Trans_AirportRunway
    c('Trans_AirportRunway', 'A', '501.1', '501.5', fcode=20100),  # Runway
    c('Trans_AirportRunway', 'A', '501.1', '501.5', fcode=20101),  # Taxiway
    c('Trans_AirportRunway', 'A', '501.1', '501.5', fcode=20102),  # Apron/Hardstand
    c('Trans_AirportRunway', 'A', '501.1', '501.5', fcode=20103),  # Stopway
] + [
    # National Structures Dataset, Struct_Point: the subtypes of FType.
    c('Struct_Point', 'P', None, None, why=SITE, ftype=ftype) for ftype in (730, 740, 780, 800, 810, 820, 830)
] + [
    # Elev_Contour: normal, depression, carrying and rise contours; intermediate, index, supplemental.
    c('Elev_Contour', 'L', isom, isom, fcode=fcode) for fcode, isom in (
        (10101, '101'), (10102, '102'), (10103, '103'),
        (10104, '101'), (10105, '102'), (10106, '103'),
        (10107, '101'), (10108, '102'), (10109, '103'),
        (10113, '101'), (10114, '102'), (10115, '103'),
        (10116, '101'), (10117, '101'), (10118, '101'), (10120, '101'), (10199, '101'))
] + [
    c('Elev_Contour', 'L', None, None, why=UNDER, fcode=fcode) for fcode in (10110, 10111, 10112, 10119)   # bathymetric
]
