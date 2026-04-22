#include "controlserver.h"
#include "channelmixer.h"
#include "virtualrig.h"
#include "civemulator.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace {
// Single-page UI embedded in the binary — no external assets so the bench
// stays self-contained. Polls /api/state every 500 ms, POSTs to /api/set
// on edit. Kept tight on purpose: one file, no framework.
const char* kIndexHtml = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Virtual Rig Bench</title>
<style>
body { font: 13px/1.4 system-ui, sans-serif; margin: 20px; max-width: 1100px; color: #222; }
h1 { font-size: 18px; margin: 0 0 8px; }
h2 { font-size: 14px; margin: 24px 0 8px; color: #555; }
table { border-collapse: collapse; width: 100%; }
th, td { padding: 6px 10px; border-bottom: 1px solid #eee; text-align: left; }
th { font-weight: 600; color: #555; background: #fafafa; }
.ptt-on  { color: #c00; font-weight: bold; }
.ptt-off { color: #999; }
input[type=number] { width: 72px; padding: 2px 4px; font: inherit; }
select { font: inherit; padding: 2px 6px; }
td.src { font-weight: 600; background: #fafafa; }
.band-pill { display: inline-block; padding: 1px 8px; border-radius: 10px; background: #eef; color: #336; font-size: 11px; }
.controls { margin: 12px 0; }
.hint { color: #888; font-size: 12px; }
.grid td { text-align: center; }
</style>
</head>
<body>
<h1>Virtual Rig Bench</h1>
<p class="hint">Per-link attenuation is keyed on the <em>source</em> rig's band — when rig A transmits on 20 m, the mixer looks up the 20 m column of A's row.</p>

<h2>Rigs</h2>
<table id="rigs">
<thead><tr><th>#</th><th>Name</th><th>Freq</th><th>Band</th><th>Mode</th><th>PTT</th><th>Noise RMS</th></tr></thead>
<tbody></tbody>
</table>

<h2>Attenuation matrix (dB)</h2>
<div class="controls">
  Band: <select id="bandSel"></select>
  <span class="hint">&nbsp;rows = source rig, columns = destination rig. Empty = self (not applicable).</span>
</div>
<table id="atten" class="grid"><thead></thead><tbody></tbody></table>

<script>
const MODE_NAMES = {
  0x00:"LSB", 0x01:"USB", 0x02:"AM", 0x03:"CW", 0x04:"RTTY",
  0x05:"FM",  0x07:"CW-R", 0x08:"RTTY-R", 0x17:"DV"
};
const BANDS = ["160m","80m","60m","40m","30m","20m","17m","15m","12m","10m","6m","2m","70cm","other"];

function fmtHz(hz) {
  if (hz >= 1000000) return (hz/1000000).toFixed(3) + " MHz";
  if (hz >= 1000)    return (hz/1000).toFixed(3)    + " kHz";
  return hz + " Hz";
}
function gainToDb(g) {
  if (g <= 1e-6) return -120;
  return 20 * Math.log10(g);
}
function dbToGain(db) { return Math.pow(10, db/20); }

let state = null;
let activeEditor = null;   // DOM element currently being edited; don't overwrite

async function fetchState() {
  const r = await fetch("/api/state");
  state = await r.json();
  render();
}

async function postSet(obj) {
  await fetch("/api/set", {
    method: "POST",
    headers: {"Content-Type":"application/json"},
    body: JSON.stringify(obj)
  });
  fetchState();
}

function renderRigs() {
  const tbody = document.querySelector("#rigs tbody");
  const rows = state.rigs.map(r => {
    const editing = activeEditor && activeEditor.dataset.kind === "noise" &&
                    parseInt(activeEditor.dataset.rig) === r.idx;
    return `<tr>
      <td>${r.idx}</td>
      <td>${r.name}</td>
      <td>${fmtHz(r.freq)}</td>
      <td><span class="band-pill">${r.band}</span></td>
      <td>${MODE_NAMES[r.mode] || "0x"+r.mode.toString(16)}</td>
      <td class="${r.ptt?"ptt-on":"ptt-off"}">${r.ptt?"TX":"—"}</td>
      <td>${editing
            ? `<input type="number" data-kind="noise" data-rig="${r.idx}" value="${activeEditor.value}" min="0" max="32767" step="10">`
            : `<input type="number" data-kind="noise" data-rig="${r.idx}" value="${r.noiseRms}" min="0" max="32767" step="10">`
          }</td>
    </tr>`;
  });
  tbody.innerHTML = rows.join("");
}

function renderAtten() {
  const band = document.getElementById("bandSel").value;
  const head = document.querySelector("#atten thead");
  const body = document.querySelector("#atten tbody");
  const n = state.rigs.length;
  let hdr = "<tr><th>src \\ dst</th>";
  for (let j = 0; j < n; ++j) hdr += `<th>#${j}</th>`;
  hdr += "</tr>";
  head.innerHTML = hdr;

  let rows = "";
  for (let i = 0; i < n; ++i) {
    let r = `<tr><td class="src">#${i} ${state.rigs[i].name}</td>`;
    for (let j = 0; j < n; ++j) {
      if (i === j) { r += "<td></td>"; continue; }
      const g = state.atten[band][i][j];
      const db = gainToDb(g).toFixed(1);
      const editing = activeEditor && activeEditor.dataset.kind === "atten" &&
                      parseInt(activeEditor.dataset.src) === i &&
                      parseInt(activeEditor.dataset.dst) === j &&
                      activeEditor.dataset.band === band;
      const val = editing ? activeEditor.value : db;
      r += `<td><input type="number" data-kind="atten" data-src="${i}" data-dst="${j}" data-band="${band}" value="${val}" min="-120" max="20" step="1"></td>`;
    }
    r += "</tr>";
    rows += r;
  }
  body.innerHTML = rows;
}

function renderBandSelector() {
  const sel = document.getElementById("bandSel");
  const prev = sel.value;
  sel.innerHTML = BANDS.map(b => `<option value="${b}">${b}</option>`).join("");
  if (prev) sel.value = prev;
  else {
    // Default to whichever band rig #0 is currently on.
    if (state && state.rigs.length) sel.value = state.rigs[0].band;
  }
}

function render() {
  renderBandSelector();
  renderRigs();
  renderAtten();
}

document.addEventListener("focusin", e => {
  if (e.target.matches("input[data-kind]")) activeEditor = e.target;
});
document.addEventListener("focusout", e => {
  if (e.target === activeEditor) activeEditor = null;
});
document.addEventListener("change", e => {
  const el = e.target;
  if (!el.matches("input[data-kind]")) return;
  if (el.dataset.kind === "noise") {
    postSet({type:"noise", rig: parseInt(el.dataset.rig), rms: parseFloat(el.value)});
  } else if (el.dataset.kind === "atten") {
    postSet({type:"atten",
             src: parseInt(el.dataset.src),
             dst: parseInt(el.dataset.dst),
             band: el.dataset.band,
             gain: dbToGain(parseFloat(el.value))});
  }
});
document.getElementById("bandSel").addEventListener("change", () => {
  if (state) renderAtten();
});

fetchState();
setInterval(fetchState, 500);
</script>
</body>
</html>
)HTML";
} // namespace

controlServer::controlServer(channelMixer* mixer, QObject* parent)
    : QObject(parent), mixer(mixer)
{
}

controlServer::~controlServer()
{
    // QTcpServer + socket children are parented; Qt cleans them up.
}

bool controlServer::listen(quint16 port)
{
    server = new QTcpServer(this);
    connect(server, &QTcpServer::newConnection, this, &controlServer::onNewConnection);
    if (!server->listen(QHostAddress::Any, port)) {
        qWarning() << "controlServer: listen on" << port << "failed:" << server->errorString();
        return false;
    }
    qInfo() << "controlServer: listening on http://127.0.0.1:" << port;
    return true;
}

void controlServer::onNewConnection()
{
    while (QTcpSocket* s = server->nextPendingConnection()) {
        connect(s, &QTcpSocket::readyRead,    this, &controlServer::onReadyRead);
        connect(s, &QTcpSocket::disconnected, this, &controlServer::onDisconnected);
        buffers.insert(s, QByteArray());
    }
}

void controlServer::onDisconnected()
{
    auto* s = qobject_cast<QTcpSocket*>(sender());
    if (!s) return;
    buffers.remove(s);
    s->deleteLater();
}

void controlServer::onReadyRead()
{
    auto* s = qobject_cast<QTcpSocket*>(sender());
    if (!s) return;
    buffers[s].append(s->readAll());
    QByteArray& buf = buffers[s];

    // Wait for end-of-headers. Small request bodies (JSON) fit in one read,
    // but do a content-length check just in case.
    int hdrEnd = buf.indexOf("\r\n\r\n");
    if (hdrEnd < 0) return;

    int contentLen = 0;
    int clPos = buf.indexOf("Content-Length:");
    if (clPos < 0) clPos = buf.indexOf("content-length:");
    if (clPos >= 0 && clPos < hdrEnd) {
        int lineEnd = buf.indexOf("\r\n", clPos);
        QByteArray val = buf.mid(clPos + 15, lineEnd - clPos - 15).trimmed();
        contentLen = val.toInt();
    }
    if (buf.size() < hdrEnd + 4 + contentLen) return;

    // Parse request line.
    int firstLineEnd = buf.indexOf("\r\n");
    QByteArray reqLine = buf.left(firstLineEnd);
    QList<QByteArray> parts = reqLine.split(' ');
    if (parts.size() < 2) { s->disconnectFromHost(); return; }
    QByteArray method = parts[0];
    QByteArray path = parts[1];
    QByteArray body = buf.mid(hdrEnd + 4, contentLen);
    buf.remove(0, hdrEnd + 4 + contentLen);

    handleRequest(s, method, path, body);
}

void controlServer::handleRequest(QTcpSocket* s, const QByteArray& method,
                                  const QByteArray& path, const QByteArray& body)
{
    if (method == "GET" && (path == "/" || path == "/index.html")) {
        sendResponse(s, 200, "text/html; charset=utf-8", kIndexHtml);
        return;
    }
    if (method == "GET" && path == "/api/state") {
        sendResponse(s, 200, "application/json", renderStateJson());
        return;
    }
    if (method == "POST" && path == "/api/set") {
        QByteArray err = applySetJson(body);
        if (err.isEmpty()) sendResponse(s, 200, "application/json", "{\"ok\":true}");
        else               sendResponse(s, 400, "application/json",
                                        "{\"ok\":false,\"error\":\"" + err + "\"}");
        return;
    }
    sendResponse(s, 404, "text/plain", "not found\n");
}

void controlServer::sendResponse(QTcpSocket* s, int status,
                                 const QByteArray& contentType, const QByteArray& body)
{
    const char* reason = "OK";
    if (status == 400) reason = "Bad Request";
    if (status == 404) reason = "Not Found";
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    resp += "Content-Type: " + contentType + "\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "Connection: close\r\n\r\n";
    resp += body;
    s->write(resp);
    s->flush();
    s->disconnectFromHost();
}

QByteArray controlServer::renderStateJson() const
{
    QJsonObject root;

    QJsonArray rigs;
    for (int i = 0; i < mixer->rigCount(); ++i) {
        virtualRig* r = mixer->rigAt(i);
        QJsonObject o;
        o["idx"] = i;
        if (r) {
            o["name"] = r->config().name;
            o["freq"] = (qint64)r->freq();
            o["mode"] = (int)r->mode();
            o["band"] = channelMixer::bandName(channelMixer::bandForFreq(r->freq()));
            o["ptt"] = r->isTransmitting();
        } else {
            o["name"] = "—";
            o["freq"] = 0;
            o["mode"] = 0;
            o["band"] = "other";
            o["ptt"] = false;
        }
        o["noiseRms"] = mixer->noiseLevel(i);
        rigs.append(o);
    }
    root["rigs"] = rigs;
    root["channelRouting"] = mixer->channelRoutingEnabled();

    QJsonObject atten;
    for (int b = 0; b < channelMixer::BandCount; ++b) {
        auto band = (channelMixer::Band)b;
        QJsonArray matrix;
        for (int i = 0; i < mixer->rigCount(); ++i) {
            QJsonArray row;
            for (int j = 0; j < mixer->rigCount(); ++j) {
                if (i == j) row.append(QJsonValue());  // null
                else        row.append(mixer->linkAttenuation(i, j, band));
            }
            matrix.append(row);
        }
        atten[channelMixer::bandName(band)] = matrix;
    }
    root["atten"] = atten;

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray controlServer::applySetJson(const QByteArray& body)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) return "invalid json";
    if (!doc.isObject()) return "expected object";
    QJsonObject o = doc.object();
    QString type = o.value("type").toString();

    if (type == "noise") {
        int rig = o.value("rig").toInt(-1);
        double rms = o.value("rms").toDouble(-1);
        if (rig < 0 || rms < 0) return "bad noise params";
        mixer->setNoiseLevel(rig, (float)rms);
        return QByteArray();
    }
    if (type == "atten") {
        int src = o.value("src").toInt(-1);
        int dst = o.value("dst").toInt(-1);
        QString bandStr = o.value("band").toString();
        double gain = o.value("gain").toDouble(-1);
        if (src < 0 || dst < 0 || gain < 0) return "bad atten params";
        // Map band name → enum.
        channelMixer::Band band = channelMixer::BandOther;
        for (int b = 0; b < channelMixer::BandCount; ++b) {
            if (channelMixer::bandName((channelMixer::Band)b) == bandStr) {
                band = (channelMixer::Band)b;
                break;
            }
        }
        mixer->setLinkAttenuation(src, dst, band, (float)gain);
        return QByteArray();
    }
    return "unknown type";
}
