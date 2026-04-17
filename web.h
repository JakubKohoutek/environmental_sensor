#ifndef WEB_H
#define WEB_H

const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Environmental Sensor</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; background: #1a1a2e; color: #eee; padding: 16px; max-width: 480px; margin: 0 auto; }
    h1 { font-size: 1.3em; text-align: center; margin-bottom: 12px; color: #e94560; }
    .cards { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 16px; }
    .card { background: #16213e; border-radius: 10px; padding: 16px; text-align: center; }
    .card.wide { grid-column: span 2; }
    .card-label { font-size: 0.7em; color: #888; text-transform: uppercase; letter-spacing: 1px; }
    .card-value { font-size: 2em; font-weight: bold; margin: 6px 0 2px; }
    .card-unit { font-size: 0.8em; color: #666; }
    .temp { color: #e94560; }
    .hum { color: #4ecca3; }
    .pres { color: #6c9bff; }
    .alt { color: #f0a500; }
    .status { display: flex; justify-content: space-around; padding: 10px; background: #16213e; border-radius: 8px; font-size: 0.75em; color: #888; margin-bottom: 10px; }
    .ok { color: #4ecca3; }
    .err { color: #e94560; }
    .footer { text-align: center; font-size: 0.7em; color: #444; margin-top: 12px; }
  </style>
</head>
<body>
  <h1>Environmental Sensor</h1>
  <div class="status">
    <span>DHT22: <span id="dht-status" class="ok">OK</span></span>
    <span>BMP180: <span id="bmp-status" class="ok">OK</span></span>
    <span>Updated: <span id="updated">--</span></span>
  </div>
  <div class="cards">
    <div class="card">
      <div class="card-label">Temperature</div>
      <div class="card-value temp" id="temp">--</div>
      <div class="card-unit">&deg;C</div>
    </div>
    <div class="card">
      <div class="card-label">Humidity</div>
      <div class="card-value hum" id="hum">--</div>
      <div class="card-unit">%</div>
    </div>
    <div class="card">
      <div class="card-label">Pressure</div>
      <div class="card-value pres" id="pres">--</div>
      <div class="card-unit">hPa</div>
    </div>
    <div class="card">
      <div class="card-label">Altitude</div>
      <div class="card-value alt" id="alt">--</div>
      <div class="card-unit">m</div>
    </div>
  </div>
  <div class="footer">BMP180 temp: <span id="bmp-temp">--</span>&deg;C</div>

  <script>
    function updateStatus() {
      fetch('/api/status').then(r => r.json()).then(d => {
        document.getElementById('temp').textContent = d.temperature != null ? d.temperature.toFixed(1) : '--';
        document.getElementById('hum').textContent = d.humidity != null ? d.humidity.toFixed(1) : '--';
        document.getElementById('pres').textContent = d.pressure != null ? d.pressure.toFixed(0) : '--';
        document.getElementById('alt').textContent = d.altitude != null ? d.altitude.toFixed(0) : '--';
        document.getElementById('bmp-temp').textContent = d.bmpTemp != null ? d.bmpTemp.toFixed(1) : '--';
        var ds = document.getElementById('dht-status');
        ds.textContent = d.dhtOk ? 'OK' : 'ERR';
        ds.className = d.dhtOk ? 'ok' : 'err';
        var bs = document.getElementById('bmp-status');
        bs.textContent = d.bmpOk ? 'OK' : 'ERR';
        bs.className = d.bmpOk ? 'ok' : 'err';
        document.getElementById('updated').textContent = new Date().toLocaleTimeString();
      }).catch(() => {});
    }
    updateStatus();
    setInterval(updateStatus, 5000);
  </script>
</body>
</html>
)=====";

#endif // WEB_H
