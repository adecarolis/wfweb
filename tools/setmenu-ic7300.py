"""Usage: python tools/setmenu-ic7300.py resources/web-shared/setmenu/IC-7300.json

Generate resources/web-shared/setmenu/IC-7300.json from the IC-7300 Full
Manual (IC-7300_ENG_FM_11.pdf), section 19 "Control command": the 1A 05
command table (pp. 19-4 to 19-7) and data content description (pp. 19-9 to
19-13); names and menu paths from the Set mode (12), scope (5), keyer /
RTTY (4), recorder (6), voice TX (7) chapters.  Items 0196-0216 also appear
in the firmware Information sheets V1.30 / V1.40."""
import json
import sys

FM = "IC-7300 Full Manual rev. 11, p. "
groups = []
cur = None


def group(path):
    global cur
    cur = {"path": path, "items": []}
    groups.append(cur)


def add(it, page, **extra):
    it.update(extra)
    it.setdefault("source", FM + page)
    cur["items"].append(it)


def codes(labels, start=0):
    return [["%02d" % (start + i), l] for i, l in enumerate(labels)]


def E(no, name, labels, page, start=0, **kw):
    add({"item": no, "name": name, "type": "enum", "options": codes(labels, start)}, page, **kw)


def B(no, name, page, labels=("OFF", "ON"), **kw):
    add({"item": no, "name": name, "type": "enum", "options": codes(labels)}, page, **kw)


def LV(no, name, page, **kw):
    it = {"item": no, "name": name, "type": "level", "digits": 4, "min": 0, "max": 255}
    if "range" not in kw:
        it["percent"] = True
    add(it, page, **kw)


def N(no, name, page, digits, lo, hi, **kw):
    add({"item": no, "name": name, "type": "number", "digits": digits, "min": lo, "max": hi}, page, **kw)


def R(no, name, page, fmt=None, **kw):
    it = {"item": no, "name": name, "type": "raw"}
    if fmt:
        it["format"] = fmt
    add(it, page, **kw)


TONE = ["%+d" % v if v else "0" for v in range(-5, 6)]
HI = {0: 2500, 1: 2700, 2: 2800, 3: 2900}
LO = {0: 100, 1: 200, 2: 300, 3: 500}
TBW = [["%d%d" % (h, l), "%d - %d Hz" % (LO[l], HI[h])] for h in HI for l in LO]
TBW_NOTE = "one byte: high digit = higher edge (2500/2700/2800/2900 Hz), low digit = lower edge (100/200/300/500 Hz) (p. 19-9); the defaults read 30 / 12 / 03 / 12 on firmware 1.42"
HPF_NOTE = "two bytes: HPF (00 = through, 01-20 = 100-2000 Hz), LPF (05-24 = 500-2400 Hz, 25 = through) (p. 19-9); read-only here until the pair is edited as one"
TXD = ["OFF", "10 ms", "15 ms", "20 ms", "25 ms", "30 ms"]
LINK = "changing it can cut wfweb's own CI-V link to the radio"
KEYS = "a USB control line that keys the radio; wfweb's serial port drives DTR/RTS, so this is read-only here"

# ---------------------------------------------------------------- Tone Control/TBW
group("SET > Tone Control/TBW > RX")
R("0001", "SSB RX HPF/LPF", "19-4", "hpflpf", note=HPF_NOTE)
E("0002", "SSB RX Bass", TONE, "19-4")
E("0003", "SSB RX Treble", TONE, "19-4")
R("0004", "AM RX HPF/LPF", "19-4", "hpflpf", note=HPF_NOTE)
E("0005", "AM RX Bass", TONE, "19-4")
E("0006", "AM RX Treble", TONE, "19-4")
R("0007", "FM RX HPF/LPF", "19-4", "hpflpf", note=HPF_NOTE)
E("0008", "FM RX Bass", TONE, "19-4")
E("0009", "FM RX Treble", TONE, "19-4")
R("0010", "CW RX HPF/LPF", "19-4", "hpflpf", note=HPF_NOTE)
R("0011", "RTTY RX HPF/LPF", "19-4", "hpflpf", note=HPF_NOTE)

group("SET > Tone Control/TBW > TX")
E("0012", "SSB TX Bass", TONE, "19-4")
E("0013", "SSB TX Treble", TONE, "19-4")
add({"item": "0014", "name": "SSB TBW (WIDE)", "type": "enum", "options": TBW}, "19-4", note=TBW_NOTE)
add({"item": "0015", "name": "SSB TBW (MID)", "type": "enum", "options": TBW}, "19-4", note=TBW_NOTE)
add({"item": "0016", "name": "SSB TBW (NAR)", "type": "enum", "options": TBW}, "19-4", note=TBW_NOTE)
add({"item": "0196", "name": "SSB-D TBW", "type": "enum", "options": TBW}, "19-7", firmware="1.30+", note=TBW_NOTE)
E("0017", "AM TX Bass", TONE, "19-4")
E("0018", "AM TX Treble", TONE, "19-4")
E("0019", "FM TX Bass", TONE, "19-4")
E("0020", "FM TX Treble", TONE, "19-4")

# ---------------------------------------------------------------- Function
group("SET > Function")
LV("0021", "Beep Level", "19-4")
B("0022", "Beep Level Limit", "19-4")
B("0023", "Beep (Confirmation)", "19-4")
E("0024", "Band Edge Beep", ["OFF", "ON (Default)", "ON (User)", "ON (User) & TX Limit"], "19-4")
E("0025", "RF/SQL Control", ["Auto", "SQL", "RF+SQL"], "19-4")
E("0193", "MF Band ATT", ["OFF", "ON"], "19-7")
E("0026", "TX Delay (HF)", TXD, "19-4")
E("0027", "TX Delay (50M)", TXD, "19-4")
E("0028", "TX Delay (70M)", TXD, "19-4", note="70 MHz is on some versions only; an IC-7300 without the 70 MHz band (firmware 1.42) answers NG")
E("0029", "Time-Out Timer (CI-V)", ["OFF", "3 min", "5 min", "10 min", "20 min", "30 min"], "19-4")

group("SET > Function > SPLIT")
B("0030", "Quick SPLIT", "19-4")
R("0031", "FM SPLIT Offset (HF)", "19-4", "offset", note="3 BCD bytes, least significant first, then 00 = + / 01 = - (p. 19-10)")
R("0032", "FM SPLIT Offset (50M)", "19-4", "offset", note="3 BCD bytes, least significant first, then 00 = + / 01 = - (p. 19-10)")
B("0033", "SPLIT LOCK", "19-4")

group("SET > Function > Tuner")
E("0034", "[TUNER] Switch", ["Manual", "Auto"], "19-4")
B("0035", "PTT Start", "19-4")

group("SET > Function > RTTY")
E("0036", "RTTY Mark Frequency", ["1275 Hz", "1615 Hz", "2125 Hz"], "19-4")
E("0037", "RTTY Shift Width", ["170 Hz", "200 Hz", "425 Hz"], "19-4")
E("0038", "RTTY Keying Polarity", ["Normal", "Reverse"], "19-4")

group("SET > Function > SPEECH")
E("0039", "SPEECH Language", ["English", "Japanese"], "19-4")
E("0040", "SPEECH Speed", ["Slow", "Fast"], "19-4")
B("0041", "S-Level SPEECH", "19-4")
B("0042", "MODE SPEECH", "19-4")
LV("0043", "SPEECH Level", "19-4")
E("0044", "[SPEECH/LOCK] Switch", ["SPEECH/LOCK (push: SPEECH, hold: LOCK)", "LOCK/SPEECH (push: LOCK, hold: SPEECH)"], "19-4")

group("SET > Function")
E("0045", "Lock Function", ["MAIN DIAL", "PANEL"], "19-4")
E("0046", "Memo Pad Quantity", ["5", "10"], "19-4")
E("0047", "MAIN DIAL Auto TS", ["OFF", "Low", "High"], "19-4")
E("0048", "MIC Up/Down Speed", ["Slow", "Fast"], "19-4")
B("0049", "Quick RIT/∂TX Clear", "19-4")
E("0050", "[NOTCH] Switch (SSB)", ["Auto", "Manual", "Auto/Manual"], "19-4")
E("0051", "[NOTCH] Switch (AM)", ["Auto", "Manual", "Auto/Manual"], "19-4")
B("0052", "SSB/CW Synchronous Tuning", "19-4")
E("0053", "CW Normal Side", ["LSB", "USB"], "19-4")
B("0054", "Screen Capture [POWER] SW", "19-4")
E("0055", "Screen Capture File Type", ["PNG", "BMP"], "19-4")
E("0056", "Keyboard Type", ["Ten-key", "Full Keyboard"], "19-4")
E("0194", "Screen Full Keyboard Layout", ["English", "German", "French"], "19-7")
B("0057", "Calibration Marker", "19-4")
LV("0058", "REF Adjust", "19-4", restore=False,
   note="the reference oscillator: this radio's own calibration, so a restore leaves it alone")

group("SET > Function > Front Key Customize")
FKEY = ["PRESET", "Voice/Keyer/RTTY Memory 1", "Voice/Keyer/RTTY Memory 2", "Voice/Keyer/RTTY Memory 3", "Voice/Keyer/RTTY Memory 4"]
E("0198", "[VOX/BK-IN]", ["VOX/BK-IN"] + FKEY, "19-12", firmware="1.40+")
E("0199", "[AUTOTUNE]", ["AUTOTUNE"] + FKEY, "19-12", firmware="1.40+")
E("0200", "[▲]", ["M-CH UP", "M-CH DOWN"] + FKEY, "19-12", firmware="1.40+")
E("0201", "[▼]", ["M-CH UP", "M-CH DOWN"] + FKEY, "19-12", firmware="1.40+")

group("SET > Function > MIC Key Customize")
MKEY = ["No function", "UP", "DOWN", "UP (VFO: kHz)", "DOWN (VFO: kHz)", "XFC", "VFO/MEMO", "BAND UP", "BAND DOWN",
        "SPEECH", "MODE", "Voice/Keyer/RTTY Memory 1", "Voice/Keyer/RTTY Memory 2", "Voice/Keyer/RTTY Memory 3",
        "Voice/Keyer/RTTY Memory 4", "TS", "MPAD", "SPLIT", "A/B", "TUNER"]
MNOTE = "codes from the V1.40 Information sheet (p. 9); the full manual rev. 11 p. 19-12 prints 09 as BAND DOWN and stops at 18"
E("0202", "[UP]", MKEY, "19-12", firmware="1.40+", note=MNOTE)
E("0203", "[DN]", MKEY, "19-12", firmware="1.40+", note=MNOTE)

# ---------------------------------------------------------------- Connectors
group("SET > Connectors")
E("0059", "ACC/USB Output Select", ["AF", "IF"], "19-4")
LV("0060", "ACC/USB AF Output Level", "19-4")
E("0061", "ACC/USB AF SQL", ["OFF (OPEN)", "ON"], "19-5",
  note="rigs/IC-7300.rig names 1A 05 0061 'CI-V Output (ANT)': that is 0073")
B("0062", "ACC/USB AF Beep/Speech... Output", "19-5")
LV("0063", "ACC/USB IF Output Level", "19-5")
LV("0064", "ACC MOD Level", "19-5")
LV("0065", "USB MOD Level", "19-5", note="wfweb's MIC slider sets this one on a USB-connected IC-7300")
MOD = ["MIC", "ACC", "MIC,ACC", "USB", "MIC,USB"]
E("0066", "DATA OFF MOD", MOD, "19-5")
E("0067", "DATA MOD", MOD, "19-5")

group("SET > Connectors > External Keypad")
B("0068", "VOICE", "19-5")
B("0069", "KEYER", "19-5")
B("0070", "RTTY", "19-5")

group("SET > Connectors > CI-V")
B("0071", "CI-V Transceive", "19-5", readonly=True, note=LINK)
N("0072", "CI-V USB→REMOTE Transceive Address", "19-5", 4, 0, 223,
  note="the address in decimal BCD: 0000 = 00h to 0223 = DFh")
B("0073", "CI-V Output (for ANT)", "19-5")
E("0074", "CI-V USB Port", ["Link to [REMOTE]", "Unlink from [REMOTE]"], "19-5", readonly=True,
  note="read only over CI-V (p. 19-5)")
E("0075", "CI-V USB Echo Back", ["ON", "OFF"], "19-5", readonly=True, note=LINK)

group("SET > Connectors")
E("0076", "USB Serial Function", ["CI-V", "RTTY Decode"], "19-5", readonly=True,
  note="RTTY Decode takes the USB serial port away from CI-V: " + LINK)
E("0077", "RTTY Decode Baud Rate", ["4800", "9600", "19200", "38400"], "19-5")

group("SET > Connectors > USB SEND/Keying")
LINES = ["OFF", "DTR", "RTS"]
E("0078", "USB SEND", LINES, "19-5", readonly=True, note=KEYS)
E("0079", "USB Keying (CW)", LINES, "19-5", readonly=True, note=KEYS)
E("0080", "USB Keying (RTTY)", LINES, "19-5", readonly=True, note=KEYS)
B("0197", "Inhibit Timer at USB Connection", "19-7", firmware="1.30+")

# ---------------------------------------------------------------- Display
group("SET > Display")
LV("0081", "LCD Backlight", "19-5")
E("0082", "Display Type", ["A", "B"], "19-5")
E("0083", "Display Font", ["Basic", "Round"], "19-5")
B("0084", "Meter Peak Hold", "19-5")
B("0085", "Memory Name", "19-5")
B("0086", "MN-Q Popup (MN OFF→ON)", "19-5")
B("0087", "BW Popup (PBT)", "19-5")
B("0088", "BW Popup (FIL)", "19-5")
E("0089", "Screen Saver", ["OFF", "15 min", "30 min", "60 min"], "19-5")
B("0090", "Opening Message", "19-5")
R("0091", "My Call (opening message text)", "19-5", "ascii", note="up to 10 characters, ASCII (p. 19-10)")
B("0092", "Power ON Check", "19-5")
E("0093", "Display Language", ["English", "Japanese"], "19-5")

# ---------------------------------------------------------------- Time Set
group("SET > Time Set")
R("0094", "Date", "19-5", "date", restore=False, note="the clock: a restore leaves it alone")
R("0095", "Time", "19-5", "time", restore=False, note="the clock: a restore leaves it alone")
R("0096", "UTC Offset", "19-5", "utcOffset", note="offset 0000-1400 (hhmm), then 00 = + / 01 = - (p. 19-13)")

# ---------------------------------------------------------------- Scope
group("SCOPE > SCOPE SET")
B("0097", "Scope during TX (CENTER TYPE)", "19-5")
E("0098", "Max Hold", ["OFF", "10s Hold", "ON"], "19-5",
  note="the command table says 00-02 but lists only OFF / ON; the codes follow the Set screen (OFF, 10s Hold, ON), and the default (10s Hold) reads 01 on firmware 1.42")
E("0099", "CENTER Type Display", ["Filter Center", "Carrier Point Center", "Carrier Point Center (Abs. Freq.)"], "19-5")
E("0100", "Marker Position (FIX Type/SCROLL Type)", ["Filter Center", "Carrier Point"], "19-5")
E("0101", "VBW", ["Narrow", "Wide"], "19-5")
E("0102", "Averaging", ["OFF", "2", "3", "4"], "19-5")
E("0103", "Waveform Type", ["Fill", "Fill+Line"], "19-5")
R("0104", "Waveform Color (Current)", "19-5", "rgb")
R("0105", "Waveform Color (Line)", "19-5", "rgb")
R("0106", "Waveform Color (Max Hold)", "19-5", "rgb")
B("0107", "Waterfall Display", "19-5")
E("0108", "Waterfall Speed", ["Slow", "Mid", "Fast"], "19-5")
E("0109", "Waterfall Size (Expand Screen)", ["Small", "Mid", "Larger"], "19-5")
E("0110", "Waterfall Peak Color Level", ["Grid %d" % i for i in range(1, 9)], "19-5")
B("0111", "Waterfall Marker Auto-hide", "19-5")

group("SCOPE > SCOPE SET > Fixed Edges")
BANDS = ["0.03 - 1.60", "1.60 - 2.00", "2.00 - 6.00", "6.00 - 8.00", "8.00 - 11.00", "11.00 - 15.00", "15.00 - 20.00",
         "20.00 - 22.00", "22.00 - 26.00", "26.00 - 30.00", "30.00 - 45.00", "45.00 - 60.00", "60.00 - 74.80"]
ENOTE = "lower and higher edge, 3 BCD bytes each, least significant first (p. 19-9)"
for bi, band in enumerate(BANDS):
    for e in range(3):
        R("%04d" % (112 + bi * 3 + e), "%s MHz, Edge %d" % (band, e + 1), "19-5" if 112 + bi * 3 + e <= 123 else "19-6",
          "edges", note=ENOTE)
    R("%04d" % (204 + bi), "%s MHz, Edge 4" % band, "19-7", "edges", firmware="1.40+", note=ENOTE)

group("AUDIO > AUDIO SCOPE SET")
E("0151", "FFT Scope Waveform Type", ["Fill", "Fill+Line"], "19-6")
R("0152", "FFT Scope Waveform Color", "19-6", "rgb")
B("0153", "FFT Scope Waterfall Display", "19-6")
R("0154", "Oscilloscope Waveform Color", "19-6", "rgb")

# ---------------------------------------------------------------- Keyer
group("KEYER > EDIT/SET > 001 SET")
E("0155", "Number Style", ["Normal", "190→ANO", "190→ANT", "90→NO", "90→NT"], "19-6")
E("0156", "Count Up Trigger", ["M%d" % i for i in range(1, 9)], "19-6", start=1)
N("0157", "Present Number", "19-6", 4, 1, 9999)

group("KEYER > EDIT/SET > CW-KEY SET")
LV("0158", "Side Tone Level", "19-6")
B("0159", "Side Tone Level Limit", "19-6")
N("0160", "Keyer Repeat Time", "19-6", 2, 1, 60, unit="s")
N("0161", "Dot/Dash Ratio", "19-6", 2, 28, 45, scale=0.1, decimals=1, unit="(1:1:x)")
E("0162", "Rise Time", ["2 ms", "4 ms", "6 ms", "8 ms"], "19-6")
E("0163", "Paddle Polarity", ["Normal", "Reverse"], "19-6")
E("0164", "Key Type", ["Straight", "Bug", "Paddle"], "19-6")
B("0165", "MIC Up/Down Keyer", "19-6")

# ---------------------------------------------------------------- RTTY decode
group("DECODE > RTTY DECODE SET")
E("0166", "FFT Scope Averaging", ["OFF", "2", "3", "4"], "19-6")
R("0167", "FFT Scope Waveform Color", "19-6", "rgb")
B("0168", "Decode USOS", "19-6")
E("0169", "Decode New Line Code", ["CR, LF, CR+LF", "CR+LF"], "19-6")
B("0170", "TX USOS", "19-6")
R("0171", "Font Color (Receive)", "19-6", "rgb")
R("0172", "Font Color (Transmit)", "19-6", "rgb")

group("DECODE > Log Set")
B("0173", "Decode Log", "19-6")
E("0174", "File Type", ["Text", "HTML"], "19-6")
B("0175", "Time Stamp", "19-6")
E("0176", "Time Stamp (Time)", ["Local", "UTC"], "19-6")
B("0177", "Time Stamp (Frequency)", "19-6")

# ---------------------------------------------------------------- Scan, voice, recorder
group("SCAN > SET")
E("0178", "SCAN Speed", ["Low", "High"], "19-6")
B("0179", "SCAN Resume", "19-6")

group("VOICE > TX SET")
B("0180", "Auto Monitor", "19-6")
N("0181", "Repeat Time", "19-6", 2, 1, 15, unit="s")
LV("0195", "TX Level", "19-7")

group("RECORD > RECORDER SET")
E("0182", "REC Mode", ["TX&RX", "RX Only"], "19-6")
E("0183", "TX REC Audio", ["Direct (microphone audio)", "Monitor (TX monitor audio)"], "19-6")
E("0184", "RX REC Condition", ["Always", "Squelch Auto"], "19-6")
B("0185", "File Split", "19-6")
B("0186", "PTT Auto REC", "19-6")
E("0187", "PRE-REC for PTT Auto REC", ["OFF", "5 s", "10 s", "15 s"], "19-6")

group("RECORD > Player Set")
E("0188", "Skip Time", ["3 s", "5 s", "10 s", "30 s"], "19-6")

# ---------------------------------------------------------------- NB, VOX (MULTI)
group("MULTI > NB")
E("0189", "DEPTH", [str(i) for i in range(1, 11)], "19-6")
LV("0190", "WIDTH", "19-6", range=[1, 100])

group("MULTI > VOX")
N("0191", "DELAY", "19-6", 2, 0, 20, scale=0.1, decimals=1, unit="s")
E("0192", "VOICE DELAY", ["OFF", "Short", "Mid.", "Long"], "19-6")

table = {
    "model": "IC-7300",
    "source": "IC-7300 Full Manual rev. 11 (IC-7300_ENG_FM_11), section 19 command table and data formats; items 0196-0216 also in the firmware Information sheets V1.30 / V1.40. Formats checked by reading the factory defaults on firmware 1.42.",
    "groups": groups,
}
seen = [it["item"] for g in groups for it in g["items"]]
assert len(seen) == len(set(seen)), "duplicate item"
missing = sorted(set("%04d" % i for i in range(1, 217)) - set(seen))
print("items", len(seen), "missing", missing, file=sys.stderr)
json.dump(table, open(sys.argv[1], "w", encoding="utf-8", newline="\n"), indent=1, ensure_ascii=False)
open(sys.argv[1], "a", encoding="utf-8", newline="\n").write("\n")
