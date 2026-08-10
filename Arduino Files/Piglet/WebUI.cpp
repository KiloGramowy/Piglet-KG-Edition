#include "WebUI.h"
#include "Globals.h"
#include "Config.h"
#include "SDUtils.h"
#include "Display.h"
#include "WigleUpload.h"
#include "BleScanner.h"
#include <ArduinoJson.h>

// ---------------- Embedded HTML ----------------
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Piglet KG Edition</title>
  <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>&#x1f437;</text></svg>">
<style>
  *,*::before,*::after{box-sizing:border-box}

  :root{
    --bg:#0a0e13;
    --card:#111820;
    --cardHover:#151d28;
    --text:#e2eaf4;
    --muted:#8899ab;
    --border:#1e2a3a;
    --input:#0c1219;
    --inputBorder:#243044;
    --inputFocus:#2d4a6f;
    --accent:#2dd4bf;
    --accentDim:rgba(45,212,191,.15);
    --btn:#182030;
    --btnHover:#1f2d42;
    --btnText:#e2eaf4;
    --primary:#2dd4bf;
    --primaryText:#0a0e13;
    --danger:#fb7185;
    --dangerDim:rgba(251,113,133,.12);
    --warn:#fbbf24;
    --warnDim:rgba(251,191,36,.12);
    --ok:#2dd4bf;
    --okDim:rgba(45,212,191,.12);
    --bad:#fb7185;
    --barBg:#182030;
    --radius:10px;
    --shadow:0 4px 24px rgba(0,0,0,.35);
  }

  html{color-scheme:dark}

  body{
    font-family:'Inter',system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;
    margin:0;padding:20px;
    max-width:920px;
    margin-left:auto;margin-right:auto;
    background:var(--bg);
    color:var(--text);
    line-height:1.5;
    -webkit-font-smoothing:antialiased;
  }

  /* ---- Header ---- */
  .header{
    display:flex;align-items:center;gap:12px;
    margin-bottom:20px;padding-bottom:16px;
    border-bottom:1px solid var(--border);
  }
  .header .logo{font-size:32px;line-height:1}
  .header h1{font-size:22px;font-weight:700;margin:0;letter-spacing:-.3px}
  .header .sub{font-size:13px;color:var(--muted);margin:0}

  /* ---- Cards ---- */
  .card{
    border:1px solid var(--border);
    border-radius:14px;
    padding:18px;
    margin:14px 0;
    background:var(--card);
    box-shadow:var(--shadow);
    transition:border-color .2s;
  }
  .card:hover{border-color:color-mix(in srgb,var(--border) 60%,var(--accent))}
  .card h3{margin:0 0 14px 0;font-size:15px;font-weight:600;text-transform:uppercase;letter-spacing:.6px;color:var(--muted)}
  .card h4{margin:0 0 8px 0;font-size:14px;font-weight:600;color:var(--muted)}
  .inner-card{
    border:1px solid var(--border);
    border-radius:var(--radius);
    padding:14px;
    margin-top:14px;
    background:var(--input);
  }

  /* ---- Inputs ---- */
  input,select{
    padding:9px 12px;
    border-radius:8px;
    border:1px solid var(--inputBorder);
    width:100%;box-sizing:border-box;
    background:var(--input);
    color:var(--text);
    font-size:14px;
    transition:border-color .15s,box-shadow .15s;
    outline:none;
  }
  input:focus,select:focus{
    border-color:var(--accent);
    box-shadow:0 0 0 3px var(--accentDim);
  }
  input:disabled,select:disabled{opacity:.72;cursor:not-allowed}
  input::placeholder{color:var(--muted);opacity:.6}

  label{
    display:block;
    font-size:12px;
    font-weight:500;
    color:var(--muted);
    margin-bottom:4px;
    text-transform:uppercase;
    letter-spacing:.4px;
  }

  /* ---- Buttons ---- */
  button{
    cursor:pointer;
    padding:9px 16px;
    border-radius:8px;
    border:1px solid var(--inputBorder);
    background:var(--btn);
    color:var(--btnText);
    font-size:14px;font-weight:500;
    transition:all .12s ease;
    outline:none;
    width:100%;
  }
  button:hover{background:var(--btnHover);border-color:var(--inputFocus)}
  button:active{transform:translateY(1px)}
  button:disabled{opacity:.45;cursor:not-allowed;transform:none}

  .btn-primary{
    background:var(--primary);color:var(--primaryText);
    border-color:var(--primary);font-weight:600;
  }
  .btn-primary:hover{background:color-mix(in srgb,var(--primary) 85%,#fff);border-color:transparent}

  .btn-danger{
    color:var(--danger);border-color:color-mix(in srgb,var(--danger) 40%,transparent);
    background:var(--dangerDim);
  }
  .btn-danger:hover{background:color-mix(in srgb,var(--danger) 20%,transparent)}

  .btn-sm{padding:6px 12px;font-size:13px;width:auto}

  /* ---- Layout helpers ---- */
  .row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
  @media(max-width:520px){.row{grid-template-columns:1fr}}

  a{color:var(--accent);text-decoration:none;font-weight:500}
  a:hover{text-decoration:underline}
  code{background:var(--input);padding:2px 7px;border-radius:6px;border:1px solid var(--border);font-size:13px}

  /* ---- Status pills ---- */
  .statusGrid{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:14px}
  .pill{
    display:inline-flex;align-items:center;gap:6px;
    border:1px solid var(--border);
    border-radius:999px;padding:5px 12px;
    font-size:13px;font-weight:500;
    background:var(--input);color:var(--text);
    transition:border-color .2s,background .2s;
  }
  .pill .dot{
    width:8px;height:8px;border-radius:50%;
    background:var(--muted);
    flex-shrink:0;
  }
  .pill.ok  .dot{background:var(--ok)}
  .pill.bad .dot{background:var(--bad)}
  .pill.warn .dot{background:var(--warn)}
  .pill.ok{border-color:rgba(45,212,191,.35);background:var(--okDim)}
  .pill.bad{border-color:rgba(251,113,133,.35);background:var(--dangerDim)}
  .pill.warn{border-color:rgba(251,191,36,.35);background:var(--warnDim)}

  /* ---- Key-value grid ---- */
  .kv{display:grid;grid-template-columns:1fr;gap:8px}
  @media(min-width:700px){.kv{grid-template-columns:1fr 1fr}}
  .kv>div{
    display:flex;justify-content:space-between;align-items:center;
    gap:12px;border:1px solid var(--border);
    border-radius:8px;padding:10px 12px;
    background:var(--input);transition:border-color .15s;
  }
  .kv>div:hover{border-color:color-mix(in srgb,var(--border) 50%,var(--accent))}
  .k{color:var(--muted);font-size:13px}
  .v{font-weight:600;font-size:14px;color:var(--text);text-align:right}

  /* ---- Progress bar ---- */
  .barWrap{
    height:8px;border-radius:8px;overflow:hidden;
    margin-top:10px;background:var(--barBg);
  }
  #wigleBar{
    height:8px;width:0%;border-radius:8px;
    background:var(--accent);
    transition:width .4s ease;
  }
  #wigleBar.active{
    background:linear-gradient(90deg,var(--accent),color-mix(in srgb,var(--accent) 60%,#fff));
    animation:barPulse 1.5s ease-in-out infinite;
  }
  @keyframes barPulse{0%,100%{opacity:1}50%{opacity:.65}}

  /* ---- File list ---- */
  .file-row{
    display:flex;align-items:center;gap:10px;flex-wrap:wrap;
    padding:8px 0;
    border-bottom:1px solid var(--border);
  }
  .file-row:last-child{border-bottom:none}
  .file-name{font-weight:500;font-size:14px;min-width:0;word-break:break-all}
  .file-size{color:var(--muted);font-size:12px;white-space:nowrap}
  .file-badge{
    font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:.5px;
    padding:2px 8px;border-radius:999px;
  }
  .file-badge.log{color:var(--danger);background:var(--dangerDim);border:1px solid rgba(251,113,133,.3)}
  .file-badge.uploaded{color:var(--muted);background:rgba(136,153,171,.1);border:1px solid rgba(136,153,171,.25)}
  .file-stats{color:var(--accent);font-size:12px;font-weight:500;white-space:nowrap}
  .file-actions{margin-left:auto;display:flex;gap:6px}

  /* ---- Config form ---- */
  .cfg-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
  @media(max-width:520px){.cfg-grid{grid-template-columns:1fr}}
  .cfg-grid>div{display:flex;flex-direction:column}
  .cfg-section{grid-column:1/-1}
  .cfg-help,.field-help{font-size:12px;color:var(--muted);line-height:1.35;margin-top:6px}
  .cfg-section .cfg-grid{margin-top:12px}
  .scan-error{font-size:13px;color:var(--bad);margin-top:10px;display:none}
  .cfg-grid>.scan-error{display:none}
  .hidden{display:none!important}

  /* ---- Details/Advanced ---- */
  details summary{
    cursor:pointer;font-size:13px;font-weight:500;
    color:var(--muted);padding:4px 0;
    transition:color .15s;
  }
  details summary:hover{color:var(--text)}
  details[open] summary{margin-bottom:8px}

  /* ---- WiGLE status text ---- */
  .ok-text{color:var(--ok)} .bad-text{color:var(--bad)} .warn-text{color:var(--warn)} .muted{color:var(--muted)}

  /* ---- Utility ---- */
  .mt-sm{margin-top:10px} .mt-md{margin-top:14px}
  .gap-sm{gap:8px}
</style>
</head>
<body>

  <div class="header">
    <div class="logo">&#x1f437;</div>
    <div>
      <h1>Piglet KG Edition</h1>
<p class="sub">XIAO ESP32-C5 Wardriver &bull; based on Piglet v2.58</p>
    </div>
  </div>

  <!-- ============ STATUS ============ -->
  <div class="card">
    <h3>Status</h3>

    <div class="statusGrid">
      <div class="pill" id="pillScan"><span class="dot"></span>Scan: —</div>
      <div class="pill" id="pillSd"><span class="dot"></span>SD: —</div>
      <div class="pill" id="pillGps"><span class="dot"></span>GPS: —</div>
      <div class="pill" id="pillSta"><span class="dot"></span>STA: —</div>
      <div class="pill" id="pillWigle"><span class="dot"></span>WiGLE: —</div>
    </div>

    <div class="kv">
      <div><span class="k">2.4 GHz Found</span><span class="v" id="vFound2g">—</span></div>
      <div id="row5g"><span class="k">5 GHz Found</span><span class="v" id="vFound5g">—</span></div>
      <div><span class="k">STA IP</span><span class="v" id="vStaIp">—</span></div>
      <div><span class="k">AP Clients Seen</span><span class="v" id="vApSeen">—</span></div>
      <div><span class="k">Last Upload</span>
    </div>

    <details class="mt-md">
      <summary>Advanced Details</summary>
      <div class="kv">
        <div><span class="k">Scan Mode</span><span class="v" id="vScanMode">—</span></div>
        <div><span class="k">GPS Baud</span><span class="v" id="vGpsBaud">—</span></div>
        <div><span class="k">Home SSID</span><span class="v" id="vHomeSsid">—</span></div>
        <div><span class="k">Wardriver SSID</span><span class="v" id="vApSsid">—</span></div>
      </div>
      <div class="mt-sm">
        <a href="/status.json" target="_blank" rel="noopener">View raw status.json &rarr;</a>
      </div>
    </details>

    <div class="row mt-md">
      <button class="btn-primary" onclick="doStart()">&#9654; Start Scan</button>
      <button onclick="doStop()">&#9724; Stop Scan</button>
    </div>

    <!-- WiGLE section -->
    <div class="inner-card">
      <h4>WiGLE Upload</h4>
      <div id="wigleMsg" class="muted" style="font-size:14px">—</div>
      <div class="barWrap">
        <div id="wigleBar"></div>
      </div>
      <div class="row mt-sm">
        <button onclick="wigleTest()">Test Token</button>
        <button class="btn-primary" onclick="wigleUploadAll()">Upload All CSVs</button>
      </div>
    </div>

    <!-- WDGoWars section -->
    <div class="inner-card">
      <h4>WDGoWars</h4>
      <div id="wdgwarsMsg" class="muted" style="font-size:14px">—</div>
      <div class="row mt-sm">
        <button onclick="wdgwarsTest()">Test API Key</button>
        <button class="btn-primary" onclick="wdgwarsUploadAll()">Upload All CSVs</button>
      </div>
    </div>
  </div>

  <!-- ============ CONFIG ============ -->
  <div class="card">
    <h3>Configuration</h3>
    <div class="cfg-grid">
      <div>
        <label>WiGLE Basic Token</label>
        <input id="wigleBasicToken" placeholder="Enter 'Encoded for use' token from wigle.net/account">
        <a href="https://wigle.net/account" target="_blank" rel="noopener" style="font-size:12px;margin-top:5px;display:inline-block">&rarr; Get your API Key here</a>
      </div>
      <div>
        <label>WDGoWars API Key</label>
        <input id="wdgwarsApiKey" placeholder="API key from wdgwars.pl/profile (leave empty to disable)">
        <a href="https://wdgwars.pl/profile/" target="_blank" rel="noopener" style="font-size:12px;margin-top:5px;display:inline-block">&rarr; Get your API Key here</a>
      </div>
      <div>
        <label>Device Name</label>
        <input id="deviceName" placeholder="e.g. rover1 &mdash; identifies this device in WiGLE uploads (optional)">
      </div>
      <div><label>GPS Baud Rate</label><input id="gpsBaud" type="number" value="9600"></div>
      <div>
        <label>GPS cache duration (minutes)</label>
        <input id="gpsCacheMinutes" type="number" value="3" min="1" max="10080" step="1">
        <div class="muted" style="font-size:12px;margin-top:5px;line-height:1.35">How long Piglet may reuse the last valid GPS position after GPS fix is lost. Default: 3 minutes. KG field-testing profile: 720 minutes / 12 hours. Long values may associate detections with an older position if the device keeps moving while GPS is unavailable.</div>
      </div>
      <div><label>Home SSID</label><input id="homeSsid" placeholder="Your home Wi-Fi"></div>
      <div><label>Home PSK</label><input id="homePsk" type="password" placeholder="Password"></div>
      <div><label>Wardriver SSID</label><input id="wardriverSsid"></div>
      <div><label>Wardriver PSK</label><input id="wardriverPsk" type="password"></div>
      <div><label>Scan Mode</label>
        <select id="scanMode">
          <option value="aggressive">Aggressive</option>
          <option value="powersaving">Power Saving</option>
        </select>
      </div>
      <div class="inner-card cfg-section">
        <h4>Scanning (Solo)</h4>
        <div class="cfg-help">Choose how Piglet scans Wi-Fi. Original keeps the standard all-channel behavior. KG Recommended uses the XIAO ESP32-C5 profile validated during KG field testing. Custom gives full control over channels and dwell.</div>
        <input id="wifi24Channels" type="hidden">
        <input id="wifi5Channels" type="hidden">
        <input id="wifi24DwellMs" type="hidden">
        <input id="wifi5DwellMs" type="hidden">
        <div class="cfg-grid">
          <div>
            <label>Scanning profile</label>
            <select id="channelScanMode" onchange="updateChannelProfileVisibility()">
              <option value="original">Original Piglet</option>
              <option value="kg">KG Recommended</option>
              <option value="custom">Custom</option>
            </select>
            <div class="field-help">Original Piglet uses Piglet's normal all-channel scan behavior. KG Recommended uses the hardware-tested XIAO ESP32-C5 starting profile: 2.4 GHz 1/6/11, 5 GHz 36/40/44/48, dwell 110 ms / 100 ms. Custom allows manual channel and dwell selection.</div>
            <div id="scanProfileStateHelp" class="field-help"></div>
          </div>
          <div id="channelProfileError" class="scan-error">Choose at least one channel profile, or switch back to Original Piglet.</div>
        </div>
        <div id="soloProfileControls" class="cfg-grid">
          <div>
            <label>2.4 GHz channels</label>
            <select id="wifi24Profile" onchange="updateChannelProfileVisibility()">
              <option value="recommended">1 / 6 / 11 only - KG Recommended</option>
              <option value="all">All channels 1-14</option>
              <option value="custom">Custom</option>
              <option value="off">Off</option>
            </select>
            <input id="wifi24CustomChannels" class="mt-sm hidden" placeholder="1,6,11,13">
            <div class="field-help">1/6/11 covers the main non-overlapping 2.4 GHz channels and gives a faster sweep. All channels improves coverage of APs using intermediate channels but takes longer.</div>
          </div>
          <div>
            <label>5 GHz channels</label>
            <select id="wifi5Profile" onchange="updateChannelProfileVisibility()">
              <option value="common">Common 36 / 40 / 44 / 48 - KG Recommended</option>
              <option value="all">All supported channels</option>
              <option value="custom">Custom</option>
              <option value="off">Off</option>
            </select>
            <input id="wifi5CustomChannels" class="mt-sm hidden" placeholder="36,40,44,48,100">
            <div class="field-help">The Common profile focuses on 36-48 for a faster 5 GHz sweep. All supported channels also scans DFS and additional channels, increasing coverage but making a full cycle slower.</div>
          </div>
          <div>
            <label>2.4 GHz dwell (ms)</label>
            <select id="wifi24DwellMode" onchange="updateChannelProfileVisibility()">
              <option value="auto">Auto / scan mode default</option>
              <option value="custom">Explicit dwell</option>
            </select>
            <input id="wifi24DwellInput" class="mt-sm hidden" type="number" min="20" max="1500" step="1" value="110">
            <div class="field-help">Longer dwell improves the chance of hearing AP beacons but makes a full channel sweep slower. KG field-test starting point: 110 ms. 20 ms is experimental and may miss weaker or less frequently heard APs.</div>
          </div>
          <div>
            <label>5 GHz dwell (ms)</label>
            <select id="wifi5DwellMode" onchange="updateChannelProfileVisibility()">
              <option value="auto">Auto / scan mode default</option>
              <option value="custom">Explicit dwell</option>
            </select>
            <input id="wifi5DwellInput" class="mt-sm hidden" type="number" min="20" max="1500" step="1" value="100">
            <div class="field-help">Longer dwell improves capture reliability but slows the full 5 GHz sweep. KG field-test starting point: 100 ms. After validation we will test 60 ms and 40 ms. 20 ms is experimental.</div>
          </div>
          <div>
            <label>BLE scanning</label>
            <select id="bleEnabled">
              <option value="false">Disabled - default</option>
              <option value="true">Enabled - experimental</option>
            </select>
            <div class="field-help">Passive BLE scanning runs between Wi-Fi cycles. Initial KG test profile: 1000 ms after every 5 completed Wi-Fi cycles. Wi-Fi remains the primary scanner.</div>
          </div>
        </div>
      </div>
      <div><label>Speed Units</label>
        <select id="speedUnits">
          <option value="kmh">km/h</option>
          <option value="mph">mph</option>
        </select>
      </div>
      <div><label>Board</label>
        <select id="board">
          <option value="auto">Auto Detect</option>
          <option value="s3">XIAO S3</option>
          <option value="exp">S3 Expansion</option>
          <option value="c5">XIAO C5</option>
          <option value="c6">XIAO C6</option>
          <option value="c3">XIAO C3 (2.4 GHz, headless)</option>
        </select>
      </div>
      <div><label>Battery ADC Pin (-1 = off)</label><input id="battPin" type="number" value="-1"></div>
      <div><label>Battery Test (elapsed time log)</label>
        <select id="batteryTest">
          <option value="false">Disabled</option>
          <option value="true">Enabled</option>
        </select>
      </div>
      <div><label>Max Files to Auto-Upload at Boot (-1=all, 0=off)</label><input id="maxBootUploads" type="number" value="25" min="-1" max="999"></div>
      <div><label>Mesh Mode on Boot</label>
        <select id="meshModeOnBoot">
          <option value="none">None &mdash; Solo Wardriving (default)</option>
          <option value="node">Node &mdash; Mesh Scanner</option>
          <option value="core">Core &mdash; Mesh Coordinator</option>
        </select>
      </div>
      <div><label>Rotate Screen 180&deg; (requires reboot)</label>
        <select id="rotateScreen180">
          <option value="false">Normal</option>
          <option value="true">Rotated 180&deg;</option>
        </select>
      </div>
      <div><label>Auto-Start Wardriving After Uploads (requires reboot)</label>
        <select id="autoStartAfterUpload">
          <option value="false">Disabled &mdash; Stay on Home Wi-Fi (default)</option>
          <option value="true">Enabled &mdash; Disconnect and Wardrive Immediately</option>
        </select>
      </div>
    </div>
    <div class="row mt-md">
      <button class="btn-primary" onclick="saveCfg()">Save Config</button>
      <button onclick="saveAndReboot()" title="Save config then restart the device">Save &amp; Reboot</button>
    </div>
    <p style="margin-top:12px;font-size:13px;color:var(--muted)">Stored at <code>/wardriver.cfg</code> on the SD card. WiFi &amp; GPS changes require a reboot.<br>
    <strong>Max Files at Boot:</strong> <code>-1</code> = upload all files every boot &bull; <code>0</code> = disabled (use web buttons manually) &bull; <code>1+</code> = upload up to N files (WiGLE allows 25 API calls/day).
    <strong>Upload All CSVs</strong> buttons above always upload every file regardless of this setting.</p>
  </div>

  <!-- ============ FILES ============ -->
  <div class="card">
    <h3>SD Card Files</h3>
    <div style="display:flex;flex-wrap:wrap;gap:8px;margin-bottom:10px">
      <button class="btn-sm" onclick="loadFiles()">&#8635; Refresh</button>
      <button class="btn-sm" onclick="window.location.href='/downloadAll'">&#11015; Download All (CSV.GZ)</button>
      <button class="btn-sm btn-danger" onclick="deleteAllLogs()">&#128465; Delete All</button>
    </div>
    <div id="files" style="font-size:14px">Loading&hellip;</div>
  </div>

<!-- AP keep-alive prompt (shown ~30 s before the extended AP window expires) -->
<div id="apKeepAliveOverlay" style="display:none;position:fixed;inset:0;background:rgba(10,14,19,.92);z-index:9998;flex-direction:column;align-items:center;justify-content:center;text-align:center;gap:14px;padding:32px">
  <div style="font-size:48px">&#x23f3;</div>
  <h2 style="margin:0;font-size:22px">Stay in WebUI?</h2>
  <p style="color:var(--muted);margin:0;font-size:15px;max-width:420px">The AP is about to close and the device will start scanning. Tap "Stay" to keep the WebUI open for another 5 minutes.</p>
  <p id="apKeepAliveCountdown" style="font-size:44px;font-weight:700;color:var(--accent);margin:0">30s</p>
  <div class="row gap-sm">
    <button class="btn-primary" onclick="apKeepAliveYes()">Stay</button>
    <button onclick="apKeepAliveNo()">Start Scanning Now</button>
  </div>
</div>

<!-- Reboot overlay (hidden until saveAndReboot() is triggered) -->
<div id="rebootOverlay" style="display:none;position:fixed;inset:0;background:rgba(10,14,19,.97);z-index:9999;flex-direction:column;align-items:center;justify-content:center;text-align:center;gap:14px;padding:32px">
  <div style="font-size:48px">&#x1f437;</div>
  <h2 style="margin:0;font-size:22px">Rebooting...</h2>
  <p id="rebootMsg" style="color:var(--muted);margin:0;font-size:15px">Config saved &mdash; device is restarting.</p>
  <p id="rebootCountdown" style="font-size:44px;font-weight:700;color:var(--accent);margin:0"></p>
  <p style="color:var(--muted);font-size:13px;margin-top:6px">Reconnect and refresh once the device comes back online.</p>
</div>

<script>
/* ---- Helpers ---- */
function $(id){return document.getElementById(id)}

function setPill(id,text,cls){
  const el=$(id);if(!el)return;
  el.classList.remove('ok','bad','warn');
  if(cls)el.classList.add(cls);
  // preserve the dot span
  const dot=el.querySelector('.dot');
  el.textContent='';
  if(dot)el.appendChild(dot);
  el.appendChild(document.createTextNode(' '+text));
}

function wigleStatusText(s){
  if(s===1)return 'WiGLE: Valid';
  if(s===-1)return 'WiGLE: Invalid';
  return 'WiGLE: Unknown';
}

function setWigleMsg(t,cls){
  const el=$('wigleMsg');
  el.textContent=t;
  el.className=cls||'muted';
}

function setText(id,val){
  const el=$(id);
  if(el)el.textContent=(val===undefined||val===null||val==='')?'\u2014':String(val);
}

function formatBytes(b){
  if(b<1024)return b+' B';
  if(b<1048576)return (b/1024).toFixed(1)+' KB';
  return (b/1048576).toFixed(1)+' MB';
}

/* ---- Masked config keys that should not be filled back into form ---- */
const maskedKeys=new Set(['homePsk']);

const WIFI24_RECOMMENDED='1,6,11';
const WIFI24_ALL='1,2,3,4,5,6,7,8,9,10,11,12,13,14';
const WIFI5_COMMON='36,40,44,48';
const WIFI5_ALL='36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165,169,173,177';
const WIFI24_DWELL_RECOMMENDED='110';
const WIFI5_DWELL_RECOMMENDED='100';
const WIFI_DWELL_RECOMMENDED={wifi24DwellInput:WIFI24_DWELL_RECOMMENDED,wifi5DwellInput:WIFI5_DWELL_RECOMMENDED};
const SCAN_PROFILE_HELP={
  original:'Switch to KG Recommended or Custom to configure channel selection and dwell.',
  kg:'KG Recommended writes the hardware-tested starting profile: 2.4 GHz 1/6/11 at 110 ms and 5 GHz 36/40/44/48 at 100 ms.',
  custom:'Custom enables manual channel and dwell controls. Choose at least one band before saving.'
};

function compactChannels(v){
  return String(v||'').replace(/\s+/g,'');
}

function setChannelError(msg){
  const el=$('channelProfileError');
  if(!el)return;
  el.textContent=msg||'';
  el.style.display=msg?'block':'none';
}

function setProfileFromChannels(band,value){
  const val=compactChannels(value);
  if(band==='24'){
    const sel=$('wifi24Profile');
    if(!sel)return;
    if(val==='')sel.value='off';
    else if(val===WIFI24_RECOMMENDED)sel.value='recommended';
    else if(val===WIFI24_ALL)sel.value='all';
    else{sel.value='custom';$('wifi24CustomChannels').value=val;}
  }else{
    const sel=$('wifi5Profile');
    if(!sel)return;
    if(val==='')sel.value='off';
    else if(val===WIFI5_COMMON)sel.value='common';
    else if(val===WIFI5_ALL)sel.value='all';
    else{sel.value='custom';$('wifi5CustomChannels').value=val;}
  }
}

function setDwellFromConfig(prefix,value){
  const val=String(value??'').trim();
  const mode=$(prefix+'DwellMode');
  const input=$(prefix+'DwellInput');
  const hidden=$(prefix+'DwellMs');
  if(hidden)hidden.value=val;
  if(mode)mode.value=(val===''||val==='0')?'auto':'custom';
  if(input){
    if(val!==''&&val!=='0')input.value=val;
    else input.value=WIFI_DWELL_RECOMMENDED[prefix+'DwellInput'];
  }
}

function applyOriginalProfileValues(){
  if($('wifi24Profile'))$('wifi24Profile').value='off';
  if($('wifi5Profile'))$('wifi5Profile').value='off';
  setDwellFromConfig('wifi24','');
  setDwellFromConfig('wifi5','');
  if($('wifi24Channels'))$('wifi24Channels').value='';
  if($('wifi5Channels'))$('wifi5Channels').value='';
}

function applyKgRecommendedProfileValues(){
  if($('wifi24Profile'))$('wifi24Profile').value='recommended';
  if($('wifi5Profile'))$('wifi5Profile').value='common';
  setDwellFromConfig('wifi24',WIFI24_DWELL_RECOMMENDED);
  setDwellFromConfig('wifi5',WIFI5_DWELL_RECOMMENDED);
  if($('wifi24Channels'))$('wifi24Channels').value=WIFI24_RECOMMENDED;
  if($('wifi5Channels'))$('wifi5Channels').value=WIFI5_COMMON;
}

function applyLoadedScanProfileConfig(ch24,ch5,d24,d5){
  const v24=compactChannels(ch24);
  const v5=compactChannels(ch5);
  const d24v=String(d24??'').trim();
  const d5v=String(d5??'').trim();
  const mode=$('channelScanMode');

  if($('wifi24Channels'))$('wifi24Channels').value=v24;
  if($('wifi5Channels'))$('wifi5Channels').value=v5;
  setProfileFromChannels('24',v24);
  setProfileFromChannels('5',v5);
  setDwellFromConfig('wifi24',d24v);
  setDwellFromConfig('wifi5',d5v);

  if(mode){
    if(v24===''&&v5==='')mode.value='original';
    else if(v24===WIFI24_RECOMMENDED&&v5===WIFI5_COMMON&&d24v===WIFI24_DWELL_RECOMMENDED&&d5v===WIFI5_DWELL_RECOMMENDED)mode.value='kg';
    else mode.value='custom';
  }
  updateChannelProfileVisibility();
}

function channelsFromProfile(band){
  if(band==='24'){
    const sel=$('wifi24Profile')?.value||'recommended';
    if(sel==='recommended')return WIFI24_RECOMMENDED;
    if(sel==='all')return WIFI24_ALL;
    if(sel==='custom')return ($('wifi24CustomChannels')?.value||'').trim();
    return '';
  }
  const sel=$('wifi5Profile')?.value||'common';
  if(sel==='common')return WIFI5_COMMON;
  if(sel==='all')return WIFI5_ALL;
  if(sel==='custom')return ($('wifi5CustomChannels')?.value||'').trim();
  return '';
}

function updateChannelProfileVisibility(){
  const profile=$('channelScanMode')?.value||'original';
  const custom=profile==='custom';
  const kg=profile==='kg';
  const original=profile==='original';
  const controls=$('soloProfileControls');
  if(controls)controls.style.display='grid';
  if(original)applyOriginalProfileValues();
  if(kg)applyKgRecommendedProfileValues();

  for(const id of ['wifi24Profile','wifi5Profile','wifi24CustomChannels','wifi5CustomChannels','wifi24DwellMode','wifi24DwellInput','wifi5DwellMode','wifi5DwellInput']){
    const el=$(id);
    if(el)el.disabled=!custom;
  }

  $('wifi24CustomChannels')?.classList.toggle('hidden',!custom||($('wifi24Profile')?.value!=='custom'));
  $('wifi5CustomChannels')?.classList.toggle('hidden',!custom||($('wifi5Profile')?.value!=='custom'));
  $('wifi24DwellInput')?.classList.toggle('hidden',!(kg||(custom&&($('wifi24DwellMode')?.value==='custom'))));
  $('wifi5DwellInput')?.classList.toggle('hidden',!(kg||(custom&&($('wifi5DwellMode')?.value==='custom'))));
  const help=$('scanProfileStateHelp');
  if(help)help.textContent=SCAN_PROFILE_HELP[profile]||'';
  if(!custom)setChannelError('');
}

function prepareDwellSave(){
  const profile=$('channelScanMode')?.value||'original';
  let d24='';
  let d5='';
  if(profile==='kg'){
    d24=WIFI24_DWELL_RECOMMENDED;
    d5=WIFI5_DWELL_RECOMMENDED;
  }else if(profile==='custom'){
    d24=($('wifi24DwellMode')?.value||'auto')==='custom'?($('wifi24DwellInput')?.value||'').trim():'';
    d5=($('wifi5DwellMode')?.value||'auto')==='custom'?($('wifi5DwellInput')?.value||'').trim():'';
  }
  if($('wifi24DwellMs'))$('wifi24DwellMs').value=d24;
  if($('wifi5DwellMs'))$('wifi5DwellMs').value=d5;
}

function prepareChannelProfileSave(){
  const mode=$('channelScanMode')?.value||'original';
  let ch24='';
  let ch5='';
  if(mode==='kg'){
    ch24=WIFI24_RECOMMENDED;
    ch5=WIFI5_COMMON;
  }else if(mode==='custom'){
    ch24=channelsFromProfile('24');
    ch5=channelsFromProfile('5');
    if(ch24.trim()===''&&ch5.trim()===''){
      setChannelError('Choose at least one channel profile, or switch back to Original Piglet.');
      return false;
    }
  }
  if($('wifi24Channels'))$('wifi24Channels').value=ch24;
  if($('wifi5Channels'))$('wifi5Channels').value=ch5;
  setChannelError('');
  return true;
}

/* ---- Status ---- */
async function loadStatus(){
  try{
    const r=await fetch('/status.json');
    const j=await r.json();

    setPill('pillScan','Scan: '+(j.allowScan?'ACTIVE':'PAUSED'),j.allowScan?'ok':'warn');
    setPill('pillSd','SD: '+(j.sdOk?'OK':'FAIL'),j.sdOk?'ok':'bad');
    setPill('pillGps','GPS: '+(j.gpsFix?'LOCK':'NO FIX'),j.gpsFix?'ok':'warn');
    setPill('pillSta','STA: '+(j.wifiConnected?'CONNECTED':'OFF'),j.wifiConnected?'ok':'warn');

    const wCls=(j.wigleTokenStatus===1)?'ok':(j.wigleTokenStatus===-1?'bad':'warn');
    setPill('pillWigle',wigleStatusText(j.wigleTokenStatus),wCls);

    setText('vFound2g',j.found2g);
    setText('vFound5g',j.found5g);

    const row5g=$('row5g');
    if(row5g)row5g.style.display=(j.c5Connected||j.found5g)?'':'none';
    setText('vStaIp',j.wifiConnected?(j.staIp||'\u2014'):'\u2014');
    setText('vApSeen',j.apClientsSeen?'Yes':'No');

    const lastUp=j.uploadLastResult?j.uploadLastResult+' (HTTP '+(j.wigleLastHttpCode||'\u2014')+')':'\u2014';
    setText('vLastUpload',lastUp);

    setText('vScanMode',j?.config?.scanMode||'\u2014');
    setText('vGpsBaud',j?.config?.gpsBaud||'\u2014');
    setText('vHomeSsid',j?.config?.homeSsid||'\u2014');
    setText('vApSsid',j?.config?.wardriverSsid||'\u2014');

    // Fill config form — skip masked/secret values
    for(const k of ['wigleBasicToken','wdgwarsApiKey','deviceName','board','gpsBaud','gpsCacheMinutes','homeSsid','wardriverSsid','wardriverPsk','scanMode','bleEnabled','speedUnits','battPin','batteryTest','maxBootUploads','meshModeOnBoot','rotateScreen180','autoStartAfterUpload']){
      if(j.config&&(k in j.config)){
        const v=String(j.config[k]);
        if(maskedKeys.has(k)&&(v===''||v==='(set)'))continue;
        const el=$(k);
        if(el)el.value=v;
      }
    }
    applyLoadedScanProfileConfig(
      j?.config?.wifi24Channels||'',
      j?.config?.wifi5Channels||'',
      j?.config?.wifi24DwellMs||'',
      j?.config?.wifi5DwellMs||''
    );
  }catch(e){console.error('loadStatus',e)}
}

/* ---- Files ---- */
async function loadFiles(){
  try{
    const r=await fetch('/files.json');const j=await r.json();
    const el=$('files');
    if(!j.ok){el.textContent='SD card not available';return;}
    if(!j.files||j.files.length===0){el.textContent='No files on SD card';return;}

    el.innerHTML=j.files.map(f=>{
      const isUploaded=f.uploaded;
      let badge=isUploaded
        ?'<span class="file-badge uploaded">uploaded</span>'
        :'<span class="file-badge log">Not Uploaded</span>';
      
      const uploadBtn=isUploaded?''
        :'<button class="btn-sm" onclick="wigleUploadOne(\''+f.name.replace(/'/g,"\\'")+'\')">Upload</button>';
      return '<div class="file-row">'
        +'<a href="/download?name='+encodeURIComponent(f.name)+'">'+f.name+'</a> '
        +badge
        +'<span class="file-size">'+formatBytes(f.size)+'</span>'
        +'<span class="file-actions">'
        +uploadBtn
        +'<button class="btn-sm btn-danger" onclick="delFile(\''+f.name.replace(/'/g,"\\'")+'\')">Delete</button>'
        +'</span></div>';
    }).join('');
  }catch(e){console.error('loadFiles',e)}
}

/* ---- Actions ---- */
async function doStart(){
  await fetch('/start',{method:'POST'});
  await loadStatus();
}
async function doStop(){
  await fetch('/stop',{method:'POST'});
  await loadStatus();
}

async function delFile(name){
  if(!confirm('Delete '+name+'?'))return;
  const el=$('files');
  const prev=el.innerHTML;
  el.innerHTML='<span style="color:var(--muted)">Deleting...</span>';
  try{
    const r=await fetch('/delete?name='+encodeURIComponent(name),{method:'POST'});
    if(!r.ok) el.innerHTML='<span style="color:var(--bad)">Delete failed ('+r.status+')</span>';
  }catch(e){
    el.innerHTML='<span style="color:var(--bad)">Delete error: '+e+'</span>';
    return;
  }
  await loadFiles();
}

async function deleteAllLogs(){
  if(!confirm('Delete ALL log files from /logs and /uploaded?\nThis cannot be undone.'))return;
  const el=$('files');
  el.innerHTML='<span style="color:var(--muted)">Deleting all logs\u2026</span>';
  try{
    const r=await fetch('/deleteAll',{method:'POST'});
    const j=await r.json().catch(()=>null);
    if(!r.ok){el.innerHTML='<span style="color:var(--bad)">Delete failed ('+r.status+')</span>';return;}
  }catch(e){
    el.innerHTML='<span style="color:var(--bad)">Error: '+e+'</span>';
    return;
  }
  await loadFiles();
}

/* ---- Shared save logic used by both Save and Save+Reboot ---- */
async function doSave(){
  if(!prepareChannelProfileSave())throw new Error('Choose at least one channel profile, or switch back to Original Piglet.');
  prepareDwellSave();
  const keys=['board','wigleBasicToken','wdgwarsApiKey','deviceName','gpsBaud','gpsCacheMinutes','wifi24Channels','wifi5Channels','wifi24DwellMs','wifi5DwellMs','homeSsid','homePsk','wardriverSsid','wardriverPsk','scanMode','bleEnabled','speedUnits','battPin','batteryTest','maxBootUploads','meshModeOnBoot','rotateScreen180','autoStartAfterUpload'];
  let body='# Saved from Web UI\n# key=value\n';
  for(const k of keys){
    const el=$(k);
    const v=el?(el.value??''):'';
    if(maskedKeys.has(k)&&v==='')continue;
    body+=k+'='+String(v).replace(/\r?\n/g,' ')+'\n';
  }
  const r=await fetch('/saveConfig',{method:'POST',headers:{'Content-Type':'text/plain'},body});
  await loadStatus();
  return r;
}

async function saveCfg(){
  try{
    await doSave();
    alert('Config saved. Reboot the device to apply WiFi / GPS changes.');
  }catch(e){alert('Save failed: '+e);}
}

async function saveAndReboot(){
  try{ await doSave(); }catch(e){ alert('Save failed: '+e); return; }

  // Show the reboot overlay
  const ov=$('rebootOverlay');
  ov.style.display='flex';

  // Ask the device to reboot (fire-and-forget; it will restart before responding)
  try{ await fetch('/reboot',{method:'POST'}); }catch(e){}

  // Count down while the device restarts (~10 s)
  let t=12;
  $('rebootCountdown').textContent=t+'s';
  const iv=setInterval(()=>{
    t--;
    $('rebootCountdown').textContent=t>0?t+'s':'';
    if(t<=0){
      clearInterval(iv);
      $('rebootMsg').textContent='Device rebooted. Reconnect and refresh this page.';
      // Attempt to auto-reload after a further 2 s
      setTimeout(()=>location.reload(),2000);
    }
  },1000);
}

function setWdgwarsMsg(t,cls){
  const el=$('wdgwarsMsg');
  el.textContent=t;
  el.className=cls||'muted';
}

async function wdgwarsTest(){
  setWdgwarsMsg('Testing key\u2026');
  try{
    const r=await fetch('/wdgwars/test',{method:'POST'});
    const j=await r.json().catch(()=>({ok:false,message:'Bad response'}));
    setWdgwarsMsg(j.message||(j.ok?'Key valid':'Key invalid'),j.ok?'ok-text':'bad-text');
  }catch(e){setWdgwarsMsg('Request failed','bad-text')}
}

async function wdgwarsUploadAll(){
  setWdgwarsMsg('Uploading all CSVs to WDGoWars\u2026');
  try{
    const r=await fetch('/wdgwars/uploadAll',{method:'POST'});
    const j=await r.json().catch(()=>null);
    if(j){
      const msg=j.message||(j.ok?j.uploaded+' file(s) uploaded':'Upload failed');
      setWdgwarsMsg(msg,j.ok?'ok-text':'bad-text');
    }
  }catch(e){setWdgwarsMsg('Upload request failed','bad-text')}
  await loadFiles();
}

async function wigleTest(){
  setWigleMsg('Testing token\u2026');
  try{
    const r=await fetch('/wigle/test',{method:'POST'});
    const j=await r.json().catch(()=>({ok:false,message:'Bad response'}));
    setWigleMsg(j.message||(j.ok?'Token valid':'Token invalid'),j.ok?'ok-text':'bad-text');
  }catch(e){setWigleMsg('Request failed','bad-text')}
  await loadStatus();
}

async function wigleUploadAll(){
  setWigleMsg('Starting upload\u2026');
  try{
    const r=await fetch('/wigle/uploadAll',{method:'POST'});
    const j=await r.json().catch(()=>null);
    if(j){
      const msg=j.message||(j.ok?j.uploaded+' file(s) uploaded':'Upload failed');
      setWigleMsg(msg,j.ok?'ok-text':'bad-text');
    }
  }catch(e){setWigleMsg('Upload request failed','bad-text')}
  await loadFiles();
  await loadStatus();
}

async function wigleUploadOne(name){
  setWigleMsg('Uploading '+name+'\u2026');
  try{
    const r=await fetch('/wigle/upload?name='+encodeURIComponent(name),{method:'POST'});
    const j=await r.json().catch(()=>null);
    if(j)setWigleMsg(j.message||(j.ok?'Uploaded':'Failed'),j.ok?'ok-text':'bad-text');
  }catch(e){setWigleMsg('Upload failed','bad-text')}
  await loadFiles();
  await loadStatus();
}

/* ---- AP keep-alive modal ----
   The firmware reports apActive / apRemainingMs / apExtendPromptLeadMs on
   /status.json. When the AP is up and remaining time is at or below the lead
   window, we show the modal. "Stay" hits /extend (resets the 5 min timer).
   "Start Scanning Now" hits /start (force-closes the AP). */
let apModalShown=false;
let apModalDismissed=false;
let apModalIv=null;

function hideApKeepAlive(){
  const ov=$('apKeepAliveOverlay');
  if(ov)ov.style.display='none';
  if(apModalIv){clearInterval(apModalIv);apModalIv=null;}
  apModalShown=false;
}

function showApKeepAlive(initialRemainingMs){
  const ov=$('apKeepAliveOverlay');
  if(!ov||apModalShown)return;
  apModalShown=true;
  ov.style.display='flex';
  let remaining=Math.max(0,Math.ceil(initialRemainingMs/1000));
  $('apKeepAliveCountdown').textContent=remaining+'s';
  apModalIv=setInterval(()=>{
    remaining--;
    if(remaining<=0){clearInterval(apModalIv);apModalIv=null;return;}
    $('apKeepAliveCountdown').textContent=remaining+'s';
  },1000);
}

async function apKeepAliveYes(){
  try{await fetch('/extend',{method:'POST'});}catch(e){}
  apModalDismissed=true;  // require remaining to recover before re-showing
  hideApKeepAlive();
}
async function apKeepAliveNo(){
  hideApKeepAlive();
  try{await fetch('/start',{method:'POST'});}catch(e){}
  // AP will go down; the page becomes unreachable until reconnect.
}

function handleApTimer(j){
  if(!j||!j.apActive){hideApKeepAlive();return;}
  const remaining=j.apRemainingMs|0;
  const lead=j.apExtendPromptLeadMs|30000;
  // Re-arm the prompt once remaining recovers above the lead (post-extend).
  if(remaining>lead)apModalDismissed=false;
  if(!apModalDismissed&&remaining>0&&remaining<=lead){
    showApKeepAlive(remaining);
  }
}

/* ---- Upload progress poller (also drives the AP keep-alive modal) ---- */
async function pollUpload(){
  try{
    const r=await fetch('/status.json');const j=await r.json();
    const up=!!j.uploading;
    const done=j.uploadDoneFiles||0;
    const total=j.uploadTotalFiles||0;
    const pct=total>0?Math.round((done/total)*100):0;

    const bar=$('wigleBar');
    bar.style.width=pct+'%';
    bar.classList.toggle('active',up);

    if(up)setWigleMsg('Uploading\u2026 '+done+'/'+total+' ('+pct+'%)');

    handleApTimer(j);
  }catch(e){/* silent */}
}
setInterval(pollUpload,1500);
loadStatus();loadFiles();
</script>
</body>
</html>
)HTML";

// ---------------- Handlers ----------------

static void handlePing() {
  Serial.printf("[WEB] /ping  heap=%u\n", ESP.getFreeHeap());
  server.send(200, "text/plain", "pong");
}

static void handleRoot() {
  Serial.printf("[WEB] / requested  heap=%u\n", ESP.getFreeHeap());
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", INDEX_HTML);
  Serial.println("[WEB] / send_P done");
}

static String channelListToString(const uint8_t* channels, uint8_t count) {
  String s;
  for (uint8_t i = 0; i < count; i++) {
    if (i > 0) s += ",";
    s += String(channels[i]);
  }
  return s;
}

static void handleStatus() {
  // Heap allocation: StaticJsonDocument<N> puts N bytes on the Arduino loop
  // task stack (8192 bytes). Even 1024 bytes plus WebServer call chain overhead
  // risks a stack overflow.  DynamicJsonDocument allocates from the heap instead.
  DynamicJsonDocument doc(2560);

  bool allowScan = scanningEnabled && sdOk && (userScanOverride || !autoPaused);
  doc["scanningEnabled"] = scanningEnabled;
  doc["allowScan"] = allowScan;
  doc["userScanOverride"] = userScanOverride;
  doc["autoPaused"] = autoPaused;
  doc["sdOk"] = sdOk;
  doc["gpsFix"] = gpsHasFix;
  doc["found2g"] = networksFound2G;
  doc["found5g"] = wardriverIsC5() ? networksFound5G : 0;
  doc["c5Connected"] = wardriverIsC5();
  doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
  doc["staIp"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
  doc["apClientsSeen"] = apClientSeen;

  // AP timer state for the WebUI's keep-alive modal.
  doc["apActive"]   = apWindowActive;
  doc["apExtended"] = apExtended;
  if (apWindowActive) {
    uint32_t elapsed, budget;
    if (apExtended) {
      elapsed = millis() - apExtendedStartMs;
      budget  = AP_EXTENDED_WINDOW_MS;
    } else {
      elapsed = millis() - apStartMs;
      budget  = AP_WINDOW_MS;
    }
    doc["apRemainingMs"] = (elapsed >= budget) ? 0 : (budget - elapsed);
  } else {
    doc["apRemainingMs"] = 0;
  }
  doc["apExtendPromptLeadMs"] = AP_EXTEND_PROMPT_LEAD_MS;
  doc["uploading"] = uploading;
  doc["uploadTotalFiles"] = uploadTotalFiles;
  doc["uploadDoneFiles"] = uploadDoneFiles;
  doc["uploadCurrentFile"] = uploadCurrentFile;
  doc["uploadLastResult"] = uploadLastResult;
  doc["wigleTokenStatus"] = wigleTokenStatus;
  doc["wigleLastHttpCode"] = wigleLastHttpCode;

  JsonObject c = doc.createNestedObject("config");
  c["wigleBasicToken"] = cfg.wigleBasicToken;
  c["wdgwarsApiKey"]   = cfg.wdgwarsApiKey;
  c["homeSsid"] = cfg.homeSsid;
  c["homePsk"] = cfg.homePsk.length() ? "(set)" : "";
  c["wardriverSsid"] = cfg.wardriverSsid;
  c["wardriverPsk"] = cfg.wardriverPsk;
  c["gpsBaud"] = cfg.gpsBaud;
  c["gpsCacheMinutes"] = cfg.gpsCacheMinutes;
  c["wifi24Channels"] = channelListToString(cfg.wifi24Channels, cfg.wifi24ChannelCount);
  c["wifi5Channels"] = channelListToString(cfg.wifi5Channels, cfg.wifi5ChannelCount);
  c["wifi24DwellMs"] = cfg.wifi24DwellMs > 0 ? String(cfg.wifi24DwellMs) : "";
  c["wifi5DwellMs"] = cfg.wifi5DwellMs > 0 ? String(cfg.wifi5DwellMs) : "";
  c["scanMode"] = cfg.scanMode;
  c["bleEnabled"] = cfg.bleEnabled ? "true" : "false";
  c["bleScanDurationMs"] = cfg.bleScanDurationMs;
  c["bleEveryNCycles"] = cfg.bleEveryNCycles;
  c["board"] = cfg.board;
  c["speedUnits"] = cfg.speedUnits;
  c["battPin"] = cfg.battPin;
  c["batteryTest"] = cfg.batteryTest;
  c["maxBootUploads"] = cfg.maxBootUploads;
  c["deviceName"]     = cfg.deviceName;
  c["meshModeOnBoot"] = cfg.meshModeOnBoot;
  c["rotateScreen180"] = cfg.rotateScreen180;
  c["autoStartAfterUpload"] = cfg.autoStartAfterUpload;

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

static void addDirFiles(JsonArray arr, const char* dir) {
  File root = SD.open(dir);
  if (!root) return;

  bool isUploaded = (String(dir) == "/uploaded");

  File f = root.openNextFile();
  while (f) {
    const char* rawName = f.name();

    String fullPath = normalizeSdPath(dir, rawName);
    if (fullPath.length() == 0) {
      f.close();
      f = root.openNextFile();
      continue;
    }

    JsonObject o = arr.createNestedObject();
    o["name"] = fullPath;
    o["size"] = (uint32_t)f.size();
    o["uploaded"] = isUploaded;

    f.close();
    f = root.openNextFile();
  }

  root.close();
}

static void handleFiles() {
  // 4096-byte STATIC doc would use half the entire 8192-byte task stack.
  // Use dynamic (heap) allocation instead.
  DynamicJsonDocument doc(4096);
  doc["ok"] = sdOk;

  JsonArray arr = doc.createNestedArray("files");
  if (sdOk) {
    addDirFiles(arr, "/logs");
    addDirFiles(arr, "/uploaded");
  }

  // Check if JSON buffer overflowed
  if (doc.overflowed()) {
    Serial.println("[WEB] WARNING: files.json buffer overflow!");
    server.send(507, "application/json", "{\"ok\":false,\"error\":\"Buffer overflow\"}");
    return;
  }

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

static void handleDownload() {
  if (!sdOk) { server.send(500, "text/plain", "SD not available"); return; }
  if (!server.hasArg("name")) { server.send(400, "text/plain", "Missing name"); return; }
  String name = server.arg("name");
  if (!isAllowedDataPath(name)) { server.send(403, "text/plain", "Forbidden"); return; }
  if (!SD.exists(name)) { server.send(404, "text/plain", "Not found"); return; }

  File f = SD.open(name, FILE_READ);
  server.streamFile(f, "text/csv");
  f.close();
}

static void handleDelete() {
  if (!sdOk) { server.send(500, "text/plain", "SD not available"); return; }
  if (!server.hasArg("name")) { server.send(400, "text/plain", "Missing name"); return; }
  String name = server.arg("name");
  if (!isAllowedDataPath(name)) { server.send(403, "text/plain", "Forbidden"); return; }
  bool ok = SD.remove(name);
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
}

static void handleDeleteAll() {
  if (!sdOk) { server.send(500, "application/json", "{\"ok\":false,\"error\":\"SD not available\"}"); return; }

  uint32_t deleted = 0;
  const char* dirs[] = { "/logs", "/uploaded" };
  for (const char* dir : dirs) {
    File root = SD.open(dir);
    if (!root) continue;
    std::vector<String> paths;
    File f = root.openNextFile();
    while (f) {
      String p = normalizeSdPath(dir, f.name());
      f.close();
      if (p.length() > 0) paths.push_back(p);
      f = root.openNextFile();
    }
    root.close();
    for (const String& p : paths) {
      if (currentCsvPath.length() > 0 && p == currentCsvPath) continue;
      if (SD.remove(p)) deleted++;
    }
  }

  DynamicJsonDocument doc(128);
  doc["ok"] = true;
  doc["deleted"] = deleted;
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleStart() {
  scanningEnabled = true;
  userScanOverride = true;
  // If the AP is still up, request an immediate teardown so scanning can begin.
  if (apWindowActive) {
    Serial.println("[WEB] /start -> force-close AP");
    apForceClose = true;
  }
  server.send(200, "text/plain", "OK");
}

static void handleExtend() {
  if (!apWindowActive) {
    server.send(409, "text/plain", "AP not active");
    return;
  }
  apExtended = true;
  apExtendedStartMs = millis();
  Serial.println("[WEB] /extend -> AP extended window reset");
  server.send(200, "text/plain", "OK");
}

static void handleStop() {
  scanningEnabled = false;
  userScanOverride = true;
  bleScannerStop();
  server.send(200, "text/plain", "OK");
}

static void handleSaveConfig() {
  if (!sdOk) { server.send(500, "text/plain", "SD not available"); return; }

  String body = server.arg("plain");
  body.trim();
  if (body.length() == 0) { server.send(400, "text/plain", "Empty body"); return; }

  bool any = false;

  // If someone posts JSON manually, still accept it (nice fallback)
  if (body[0] == '{') {
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, body);
    if (err) { server.send(400, "text/plain", "Bad JSON"); return; }

    cfg.wigleBasicToken = doc["wigleBasicToken"] | cfg.wigleBasicToken;
    cfg.homeSsid        = doc["homeSsid"]        | cfg.homeSsid;
    cfg.homePsk         = doc["homePsk"]         | cfg.homePsk;
    cfg.wardriverSsid   = doc["wardriverSsid"]   | cfg.wardriverSsid;
    cfg.wardriverPsk    = doc["wardriverPsk"]    | cfg.wardriverPsk;
    cfg.gpsBaud         = doc["gpsBaud"]         | cfg.gpsBaud;
    cfg.scanMode        = doc["scanMode"]        | cfg.scanMode;
    cfg.bleEnabled      = doc["bleEnabled"]      | cfg.bleEnabled;
    cfg.bleScanDurationMs = doc["bleScanDurationMs"] | cfg.bleScanDurationMs;
    cfg.bleEveryNCycles = doc["bleEveryNCycles"] | cfg.bleEveryNCycles;
    validateConfig();

    any = true;
  } else {
    // Parse key=value lines
    int pos = 0;
    while (pos < body.length()) {
      int nl = body.indexOf('\n', pos);
      if (nl < 0) nl = body.length();
      String line = body.substring(pos, nl);
      pos = nl + 1;

      String k, v;
      if (parseKeyValueLine(line, k, v)) {
        cfgAssignKV(k, v);
        any = true;
      }
    }
  }

  if (!any) {
    server.send(400, "text/plain", "No valid key=value lines");
    return;
  }

  Serial.println("[CFG] Updated config from Web UI (in-RAM). Saving to SD...");
  if (!cfg.bleEnabled) bleScannerStop();
  bool ok = saveConfigToSD();
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
}

static void handleCleanup() {
  if (!sdOk) { server.send(500, "text/plain", "SD not available"); return; }
  deleteEmptyCsvs();
  server.send(200, "text/plain", "OK");
}

static void handleReboot() {
  bleScannerStop();
  closeLogFile();                      // flush & close active CSV log cleanly
  server.send(200, "text/plain", "OK");
  server.client().stop();
  delay(200);
  ESP.restart();                       // never returns
}

static void handleWigleTest() {
  bleScannerStop();
  bool ok = wigleTestToken();

  DynamicJsonDocument doc(384);
  doc["ok"] = ok;
  doc["tokenStatus"] = wigleTokenStatus;
  doc["httpCode"] = wigleLastHttpCode;
  doc["message"] = uploadLastResult;

  String out;
  serializeJson(doc, out);
  server.send(ok ? 200 : 400, "application/json", out);
}

static void handleWdgwarsTest() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(400, "application/json",
                "{\"ok\":false,\"message\":\"STA WiFi not connected\"}");
    return;
  }
  if (cfg.wdgwarsApiKey.length() < 8) {
    server.send(400, "application/json",
                "{\"ok\":false,\"message\":\"No API key configured\"}");
    return;
  }

  bleScannerStop();
  bool ok = wdgwarsTestKey();

  DynamicJsonDocument doc(256);
  doc["ok"]      = ok;
  doc["message"] = uploadLastResult;

  String out;
  serializeJson(doc, out);
  server.send(ok ? 200 : 400, "application/json", out);
}

static void handleWdgwarsUploadAll() {
  if (!sdOk) { server.send(500, "text/plain", "SD not available"); return; }
  if (WiFi.status() != WL_CONNECTED) { server.send(400, "text/plain", "STA WiFi not connected"); return; }
  if (cfg.wdgwarsApiKey.length() < 8) { server.send(400, "text/plain", "No API key configured"); return; }

  bleScannerStop();
  // Pass -1 explicitly: web-triggered uploads bypass the maxBootUploads cap.
  uint32_t okCount = uploadAllCsvsToWdgwars(-1);

  DynamicJsonDocument doc(256);
  doc["ok"]       = (okCount > 0);
  doc["uploaded"] = okCount;
  doc["total"]    = uploadTotalFiles;
  doc["message"]  = uploadLastResult;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleWigleUploadAll() {
  if (!sdOk) { server.send(500, "text/plain", "SD not available"); return; }
  if (WiFi.status() != WL_CONNECTED) { server.send(400, "text/plain", "STA WiFi not connected"); return; }

  bleScannerStop();
  // Pass -1 explicitly: web-triggered uploads are always unlimited,
  // bypassing the maxBootUploads cap which applies only at boot.
  uint32_t okCount = uploadAllCsvsToWigle(-1);

  // History will auto-refresh when user next accesses /files.json
  
  DynamicJsonDocument doc(384);
  doc["ok"] = (okCount > 0);
  doc["uploaded"] = okCount;
  doc["total"] = uploadTotalFiles;
  doc["message"] = uploadLastResult;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleWigleUploadOne() {
  if (!sdOk) { server.send(500, "text/plain", "SD not available"); return; }
  if (WiFi.status() != WL_CONNECTED) { server.send(400, "text/plain", "STA WiFi not connected"); return; }
  if (!server.hasArg("name")) { server.send(400, "text/plain", "Missing name"); return; }

  String path = server.arg("name");
  if (!SD.exists(path)) { server.send(404, "text/plain", "Not found"); return; }

  bleScannerStop();
  uploading = true;
  uploadPausedScanWasEnabled = scanningEnabled;
  scanningEnabled = false;

  uploadTotalFiles = 1;
  uploadDoneFiles = 0;
  uploadCurrentFile = path;
  updateOLED(0);

  bool ok = uploadFileToWigle(path);

  uploadDoneFiles = 1;
  updateOLED(0);

  uploading = false;
  scanningEnabled = uploadPausedScanWasEnabled;
  uploadCurrentFile = "";
  updateOLED(0);

  if (ok) {
    moveToUploaded(path);
    // History will auto-refresh when user next accesses /files.json
  }

  DynamicJsonDocument doc(384);
  doc["ok"] = ok;
  doc["httpCode"] = wigleLastHttpCode;
  doc["message"] = uploadLastResult;

  String out;
  serializeJson(doc, out);
  server.send(ok ? 200 : 500, "application/json", out);
}

// Streams all CSVs from /logs and /uploaded merged into one RFC 1952 gzip file,
// using DEFLATE stored blocks (BTYPE=00 — no compression library required).
// Non-first files have their 2-line WiGLE header stripped so the merged result
// is one valid WiGLE CSV uploadable directly to WiGLE.net.
static void handleDownloadAll() {
  if (!sdOk) { server.send(500, "text/plain", "SD not available"); return; }

  std::vector<String> paths;
  const char* dirs[] = { "/logs", "/uploaded" };
  for (const char* d : dirs) {
    File root = SD.open(d); if (!root) continue;
    File f = root.openNextFile();
    while (f) {
      // Skip subdirectories and non-CSV files
      if (!f.isDirectory()) {
        String p = normalizeSdPath(d, f.name());
        if (p.endsWith(".csv") && !(currentCsvPath.length() && p == currentCsvPath))
          paths.push_back(p);
      }
      f.close(); f = root.openNextFile();
    }
    root.close();
  }

  Serial.printf("[DOWNLOAD] Collected %u CSV file(s) for gzip\n", (unsigned)paths.size());
  if (paths.empty()) { server.send(200, "text/plain", "No CSV files to download"); return; }

  // Chunked transfer — output size not known in advance
  server.sendHeader("Content-Disposition", "attachment; filename=\"piglet_logs.csv.gz\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/gzip", "");
  WiFiClient cl = server.client();

  // Write one HTTP chunk (hex-size CRLF data CRLF)
  auto writeChunk = [&](const uint8_t* d, size_t n) {
    if (!n) return;
    char hdr[10]; snprintf(hdr, sizeof(hdr), "%X\r\n", (unsigned)n);
    cl.write((const uint8_t*)hdr, strlen(hdr));
    cl.write(d, n);
    cl.write((const uint8_t*)"\r\n", 2);
  };

  // CRC32 (ISO 3309 / gzip polynomial 0xEDB88320)
  auto crc32Up = [](uint32_t crc, const uint8_t* b, size_t n) -> uint32_t {
    static const uint32_t T[16] = {
      0x00000000,0x1DB71064,0x3B6E20C8,0x26D930AC,
      0x76DC4190,0x6B6B51F4,0x4DB26158,0x5005713C,
      0xEDB88320,0xF00F9344,0xD6D6A3E8,0xCB61B38C,
      0x9B64C2B0,0x86D3D2D4,0xA00AE278,0xBDBDF21C
    };
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
      crc = (crc >> 4) ^ T[(crc ^ b[i]) & 0xF];
      crc = (crc >> 4) ^ T[(crc ^ (b[i] >> 4)) & 0xF];
    }
    return ~crc;
  };

  // RFC 1952 gzip header (10 bytes)
  const uint8_t gzHdr[10] = {
    0x1f, 0x8b, 0x08, 0x00,   // ID1 ID2 CM FLG
    0x00, 0x00, 0x00, 0x00,   // MTIME = 0
    0x00, 0xff                 // XFL OS=unknown
  };
  writeChunk(gzHdr, 10);

  uint32_t crc   = 0;
  uint32_t isize = 0;
  uint8_t  buf[512];

  bool first = true;
  for (const String& path : paths) {
    File f = SD.open(path, FILE_READ); if (!f) continue;
    if (!first) {
      // Strip 2-line WiGLE header from non-first files
      if (f.available()) f.readStringUntil('\n');
      if (f.available()) f.readStringUntil('\n');
    }
    first = false;
    for (;;) {
      int n = f.read(buf, sizeof(buf)); if (n <= 0) break;
      // DEFLATE stored block (BFINAL=0, BTYPE=00)
      uint16_t ln = (uint16_t)n;
      uint8_t  blk[5] = {
        0x00,                    // BFINAL=0, BTYPE=00
        (uint8_t)(ln & 0xFF),   (uint8_t)(ln >> 8),   // LEN
        (uint8_t)((~ln) & 0xFF),(uint8_t)((~ln) >> 8) // NLEN
      };
      writeChunk(blk, 5);
      writeChunk(buf, (size_t)n);
      crc    = crc32Up(crc, buf, (size_t)n);
      isize += (uint32_t)n;
      yield();
    }
    f.close();
  }

  // Final empty stored block closes the DEFLATE stream (BFINAL=1, LEN=0)
  const uint8_t finalBlk[5] = { 0x01, 0x00, 0x00, 0xFF, 0xFF };
  writeChunk(finalBlk, 5);

  // Gzip trailer: CRC32 + ISIZE (both little-endian 32-bit)
  uint8_t trailer[8] = {
    (uint8_t)crc,        (uint8_t)(crc >> 8),
    (uint8_t)(crc >> 16),(uint8_t)(crc >> 24),
    (uint8_t)isize,      (uint8_t)(isize >> 8),
    (uint8_t)(isize >> 16),(uint8_t)(isize >> 24)
  };
  writeChunk(trailer, 8);

  cl.write((const uint8_t*)"0\r\n\r\n", 5);
}

// ---------------- Server Init ----------------

void startWebServer() {
  Serial.println("[WEB] Starting web server routes...");

  server.on("/", handleRoot);
  server.on("/status.json", handleStatus);
  server.on("/files.json", handleFiles);
  server.on("/download",    handleDownload);
  server.on("/downloadAll", handleDownloadAll);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/start",  HTTP_POST, handleStart);
  server.on("/stop",   HTTP_POST, handleStop);
  server.on("/extend", HTTP_POST, handleExtend);
  server.on("/saveConfig", HTTP_POST, handleSaveConfig);

  server.on("/ping",            handlePing);
  server.on("/reboot",          HTTP_POST, handleReboot);
  server.on("/cleanup",         HTTP_POST, handleCleanup);
  server.on("/deleteAll",       HTTP_POST, handleDeleteAll);
  server.on("/wigle/test",      HTTP_POST, handleWigleTest);
  server.on("/wigle/uploadAll", HTTP_POST, handleWigleUploadAll);
  server.on("/wigle/upload",    HTTP_POST, handleWigleUploadOne);
  server.on("/wdgwars/test",      HTTP_POST, handleWdgwarsTest);
  server.on("/wdgwars/uploadAll", HTTP_POST, handleWdgwarsUploadAll);

  server.begin();
  Serial.println("[WEB] Server started");
}
