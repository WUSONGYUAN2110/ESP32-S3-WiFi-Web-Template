const $ = (id) => document.getElementById(id);
const statePill = $('statePill');
const result = $('result');
let selectedOpen = false;

function showResult(text, kind = '') {
  result.textContent = text;
  result.className = `notice ${kind}`;
}

function setState(label, kind = 'pending') {
  statePill.className = `pill ${kind}`;
  statePill.querySelector('b').textContent = label;
}

async function scan() {
  const button = $('scanButton');
  button.disabled = true;
  $('networkList').innerHTML = '<div class="empty">正在扫描附近网络…</div>';
  try {
    const response = await fetch('/api/wifi/scan', { cache: 'no-store' });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || '扫描失败');
    renderNetworks(data.networks || []);
  } catch (error) {
    $('networkList').innerHTML = `<div class="empty">${error.message}，可手动填写网络名称。</div>`;
  } finally {
    button.disabled = false;
  }
}

function renderNetworks(networks) {
  const list = $('networkList');
  list.textContent = '';
  if (!networks.length) {
    list.innerHTML = '<div class="empty">未发现网络，可在下方手动输入隐藏网络。</div>';
    return;
  }
  networks.sort((a, b) => b.rssi - a.rssi).forEach((network) => {
    const item = document.createElement('button');
    item.type = 'button';
    item.className = 'network';
    const info = document.createElement('div');
    const name = document.createElement('b');
    name.textContent = network.ssid;
    const detail = document.createElement('span');
    detail.textContent = network.security;
    info.append(name, detail);
    const signal = document.createElement('div');
    signal.className = 'signal';
    signal.textContent = `${network.rssi} dBm`;
    item.append(info, signal);
    item.addEventListener('click', () => {
      document.querySelectorAll('.network').forEach((node) => node.classList.remove('selected'));
      item.classList.add('selected');
      $('ssid').value = network.ssid;
      selectedOpen = network.open;
      $('password').placeholder = network.open ? '开放网络，无需密码' : '请输入 Wi‑Fi 密码';
      if (network.open) $('password').value = '';
      $('password').focus();
    });
    list.append(item);
  });
}

async function pollState() {
  try {
    const response = await fetch('/api/wifi/state', { cache: 'no-store' });
    const state = await response.json();
    if (state.phase === 'testing') {
      setState('正在验证', 'pending');
      showResult(`正在连接 ${state.ssid}，请保持此页面打开…`);
    } else if (state.phase === 'success') {
      setState('连接成功', 'online');
      showResult(`已连接 ${state.ssid}，设备地址为 ${state.ip}。配置热点将在数秒后关闭。`, 'success');
      $('connectButton').disabled = true;
    } else if (state.phase === 'failed') {
      setState('验证失败', 'error');
      showResult(state.error || '连接失败，请检查凭据后重试。', 'error');
      $('connectButton').disabled = false;
    } else if (state.connected) {
      setState('已连接', 'online');
    } else {
      setState('等待配置', 'pending');
    }
  } catch (_) {
    // AP 关闭或无线切换时，请求失败属于预期行为。
  }
}

$('wifiForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  const ssid = $('ssid').value.trim();
  const password = $('password').value;
  if (!ssid) return showResult('请输入 Wi‑Fi 名称。', 'error');
  if (!selectedOpen && password.length > 0 && password.length < 8) return showResult('密码至少需要 8 个字符。', 'error');
  $('connectButton').disabled = true;
  showResult(`正在提交 ${ssid}…`);
  try {
    const response = await fetch('/api/wifi/connect', {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ ssid, password })
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || '提交失败');
    setState('正在验证', 'pending');
  } catch (error) {
    showResult(error.message, 'error');
    $('connectButton').disabled = false;
  }
});

$('scanButton').addEventListener('click', scan);
$('togglePassword').addEventListener('click', () => {
  const field = $('password');
  field.type = field.type === 'password' ? 'text' : 'password';
  $('togglePassword').textContent = field.type === 'password' ? '显示' : '隐藏';
});

scan();
pollState();
setInterval(pollState, 1000);
