const $ = (id) => document.getElementById(id);
let ws;
let retryTimer;
let lastStatus;
let applyingRemote = false;
const history = [];

function formatUptime(seconds) {
  if (seconds < 3600) return (seconds / 60).toFixed(1);
  return (seconds / 3600).toFixed(1);
}

function applyStatus(state) {
  lastStatus = state;
  const pill = $('statePill');
  pill.className = `pill ${state.connected ? 'online' : 'error'}`;
  pill.querySelector('b').textContent = state.connected ? '设备在线' : '网络断开';
  $('networkSummary').textContent = state.connected ? `${state.ssid} · ${state.ip}` : (state.error || '等待网络恢复');
  $('rssi').textContent = state.connected ? state.rssi_dbm : '--';
  $('heap').textContent = (state.free_heap_bytes / 1024).toFixed(0);
  $('uptime').textContent = formatUptime(state.uptime_s);
  $('uptime').nextElementSibling.textContent = state.uptime_s < 3600 ? '分钟' : '小时';
  $('clients').textContent = state.ws_clients;
  $('ssid').textContent = state.ssid || '--';
  $('ip').textContent = state.ip || '--';

  applyingRemote = true;
  $('ledOn').checked = state.led.on;
  $('ledColor').value = state.led.color;
  $('colorText').textContent = state.led.color.toUpperCase();
  $('brightness').value = state.led.brightness;
  $('brightnessText').textContent = `${state.led.brightness}%`;
  if (document.activeElement !== $('message')) $('message').value = state.message;
  applyingRemote = false;

  history.push({ rssi: state.rssi_dbm, heap: state.free_heap_bytes / 1024 });
  if (history.length > 60) history.shift();
  drawChart();
}

function connectWebSocket() {
  clearTimeout(retryTimer);
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${protocol}//${location.host}/ws`);
  ws.onmessage = (event) => {
    try { applyStatus(JSON.parse(event.data)); } catch (_) {}
  };
  ws.onclose = () => {
    $('statePill').className = 'pill error';
    $('statePill').querySelector('b').textContent = '正在重连';
    retryTimer = setTimeout(connectWebSocket, 1500);
  };
  ws.onerror = () => ws.close();
}

async function putJson(url, body) {
  const response = await fetch(url, {
    method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body)
  });
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || '请求失败');
  return data;
}

let ledTimer;
function updateLed() {
  if (applyingRemote) return;
  $('colorText').textContent = $('ledColor').value.toUpperCase();
  $('brightnessText').textContent = `${$('brightness').value}%`;
  clearTimeout(ledTimer);
  ledTimer = setTimeout(async () => {
    try {
      await putJson('/api/led', {
        on: $('ledOn').checked,
        color: $('ledColor').value,
        brightness: Number($('brightness').value)
      });
      $('ledResult').textContent = '已同步到 GPIO48';
    } catch (error) {
      $('ledResult').textContent = error.message;
    }
  }, 120);
}

function drawChart() {
  const canvas = $('chart');
  const ratio = window.devicePixelRatio || 1;
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  if (canvas.width !== width * ratio || canvas.height !== height * ratio) {
    canvas.width = width * ratio; canvas.height = height * ratio;
  }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  ctx.clearRect(0, 0, width, height);
  ctx.strokeStyle = 'rgba(148,177,209,.12)'; ctx.lineWidth = 1;
  for (let y = 20; y < height; y += 45) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke(); }
  if (history.length < 2) return;
  const draw = (key, color, min, max) => {
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
    history.forEach((point, index) => {
      const x = index * width / Math.max(59, history.length - 1);
      const normalized = Math.max(0, Math.min(1, (point[key] - min) / (max - min)));
      const y = height - 15 - normalized * (height - 30);
      index ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    });
    ctx.stroke();
  };
  draw('rssi', '#43d9c5', -100, -20);
  const heaps = history.map((p) => p.heap);
  const low = Math.min(...heaps) - 8, high = Math.max(...heaps) + 8;
  draw('heap', '#4d8dff', low, high === low ? low + 1 : high);
}

['ledOn', 'ledColor', 'brightness'].forEach((id) => $(id).addEventListener('input', updateLed));
$('messageForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  try {
    await putJson('/api/message', { message: $('message').value });
    $('messageResult').textContent = '消息已同步到所有客户端';
  } catch (error) { $('messageResult').textContent = error.message; }
});
$('reprovision').addEventListener('click', async () => {
  if (!confirm('这会清除已保存的 Wi‑Fi，并开启配置热点。是否继续？')) return;
  const response = await fetch('/api/wifi/reprovision', { method: 'POST' });
  if (response.ok) alert('设备即将断开。请连接 ESP32S3-Setup-XXXX 热点并访问 192.168.4.1。');
});
window.addEventListener('resize', drawChart);

fetch('/api/status').then((r) => r.json()).then(applyStatus).catch(() => {});
connectWebSocket();
