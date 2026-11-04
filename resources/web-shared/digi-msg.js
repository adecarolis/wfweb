// FT8 / FT4 message text, both builds: the forms a QSO goes out in and the form
// the QSO logic reads a decode in. Pure functions; tests/ft8_msg_sim.mjs runs
// them through the real codec.
//
// A 77-bit message has room for two plain calls only when both are standard
// (WSJT-X stdCall below). Anything else (AO40EUDXF, VE3/W9XYZ) needs one of two
// hashed forms, or the codec falls back to 13 characters of free text and
// "AO40EUDXF VA2BBW FN35" goes out as "AO40EUDXF VA2" (#153):
//
//   Type 1  <NONSTD> STD  grid / report   the nonstandard call as a 22-bit
//                                         hash, only a standard call in plain
//   Type 4  NONSTD <STD>  RRR / RR73 / 73 up to 11 characters in plain, the
//           <STD> NONSTD                  other call as a 12-bit hash; no room
//           CQ NONSTD                     for a report or a grid
//
// digiQsoTexts() follows genStdMsgs() in WSJT-X, whose bits ft8ts matches
// exactly (ft8code). With two nonstandard calls the protocol has no way to send
// a report at all, and a /P or /R call cannot sit next to a hash in Type 1, so
// those fall to Type 4 too, in WSJT-X as well.

// WSJT-X MainWindow::stdCall.
function digiStdCall(call) {
    return /^([A-Z]{0,2}|[A-Z][0-9]|[0-9][A-Z])[0-9][A-Z]{0,3}(\/R|\/P)?$/.test((call || '').trim().toUpperCase());
}

// "<CALL>" -> "CALL". An unresolved hash, "<...>", stays as it is.
function digiStripHash(msg) {
    return (msg || '').replace(/<([^<>.\s]+)>/g, '$1');
}

function digiCqText(myCall, myGrid) {
    return 'CQ ' + myCall + (digiStdCall(myCall) ? ' ' + myGrid : '');
}

// The five messages of a QSO with dxCall. rpt is the report as sent ("-10").
function digiQsoTexts(dxCall, myCall, myGrid, rpt) {
    var hisStd = digiStdCall(dxCall), myStd = digiStdCall(myCall);
    var hashHis = '<' + dxCall + '> ' + myCall;
    var hashMine = dxCall + ' <' + myCall + '>';
    var plain = dxCall + ' ' + myCall;
    var withData = (hisStd && myStd) ? plain : (hisStd ? hashMine : hashHis);
    var ack = (hisStd && myStd) ? plain : ((!hisStd && myStd) ? hashMine : hashHis);
    return {
        call:    myStd ? (hisStd ? plain : hashHis) + ' ' + myGrid : hashHis,
        report:  withData + ' ' + rpt,
        rreport: withData + ' R' + rpt,
        rr73:    ack + ' RR73',
        s73:     ack + ' 73'
    };
}

// A DXpedition / multi-answer decode ("K1ABC RR73; W9XYZ <KH1/KH7Z> -08") is
// one station sending two messages in a single transmission (#129). Returns
// them in the usual "TO FROM DATA" form; anything else comes back as it is.
// Calls come out without their hash brackets either way.
function digiSplitMulti(msg) {
    msg = digiStripHash(msg);
    var m = /^(\S+)\s+RR73;\s+(\S+)\s+(\S+)\s+(\S+)$/i.exec(msg.trim());
    if (!m) return [msg];
    return [m[1] + ' ' + m[3] + ' RR73', m[2] + ' ' + m[3] + ' ' + m[4]];
}

// The form of a decode the QSO logic should read: for a multi-answer, the
// half addressed to myCall when there is one, else the first.
function digiStdMsg(msg, myCall) {
    var halves = digiSplitMulti(msg);
    var me = (myCall || '').toUpperCase();
    for (var i = 0; me && i < halves.length; i++) {
        if (halves[i].toUpperCase().indexOf(me + ' ') === 0) return halves[i];
    }
    return halves[0];
}

// The sender of a decoded FT8/FT4 message ("CQ [mod] CALL GRID", "A B rpt"),
// or '' when the message has none we can trust (free text, telemetry, an
// unresolved hash).
function digiSenderCall(msg) {
    var parts = digiSplitMulti(msg)[0].trim().toUpperCase().split(/\s+/);
    var tok = '';
    if (parts[0] === 'CQ' || parts[0] === 'QRZ' || parts[0] === 'DE') {
        // Skip modifiers (DX, POTA, FD, 145...) and grids (FN42).
        for (var p = 1; p < parts.length; p++) {
            if (/\d/.test(parts[p]) && /[A-Z]/.test(parts[p]) && !/^[A-R]{2}\d{2}$/.test(parts[p])) { tok = parts[p]; break; }
        }
    } else if (parts.length >= 2) {
        tok = parts[1];
    }
    return tok === '<...>' ? '' : tok;
}

if (typeof module !== 'undefined') {
    module.exports = { digiStdCall, digiStripHash, digiCqText, digiQsoTexts, digiSplitMulti, digiStdMsg, digiSenderCall };
}
