function refreshData() {
    fetch('/api')
        .then(response => response.json())
        .then(data => {
            document.getElementById('temperature').innerHTML = data.temperature.toFixed(1) + '<span class="sensor-unit">\u00b0C</span>';
            document.getElementById('humidity').innerHTML = data.humidity.toFixed(1) + '<span class="sensor-unit">%</span>';
            document.getElementById('soil').innerHTML = data.soil + '<span class="sensor-unit">%</span>';
            document.getElementById('ipAddress').innerText = data.ip;
            document.getElementById('wifiStatus').innerText = data.wifiStatus;

            if(data.windowOpen) {
                document.getElementById('windowStatus').innerHTML = '\ud83e\ude9f \u041e\u043a\u043d\u043e \u043e\u0442\u043a\u0440\u044b\u0442\u043e';
                document.getElementById('windowStatus').className = 'window-status window-open';
            } else {
                document.getElementById('windowStatus').innerHTML = '\ud83e\ude9f \u041e\u043a\u043d\u043e \u0437\u0430\u043a\u0440\u044b\u0442\u043e';
                document.getElementById('windowStatus').className = 'window-status window-closed';
            }
            document.getElementById('windowAngle').innerHTML = data.windowAngle + '<span class="sensor-unit">\u00b0</span>';

            if(data.led) {
                document.getElementById('ledStatus').innerHTML = '\ud83d\udca1 LED \u0432\u043a\u043b\u044e\u0447\u0435\u043d';
                document.getElementById('ledStatus').className = 'status success';
            } else {
                document.getElementById('ledStatus').innerHTML = '\u26ab\ufe0f LED \u0432\u044b\u043a\u043b\u044e\u0447\u0435\u043d';
                document.getElementById('ledStatus').className = 'status error';
            }

            document.getElementById('fanSpeed').value = data.fan;
            let percent = Math.round((data.fan / 255) * 100);
            document.getElementById('speedDisplay').innerHTML = percent;
        })
        .catch(error => console.error('\u041e\u0448\u0438\u0431\u043a\u0430:', error));
}

function controlWindow(action) {
    fetch(`/window?action=${action}`)
        .then(() => refreshData());
}

function controlLED(state) {
    fetch(`/led?state=${state}`)
        .then(() => refreshData());
}

function controlFan(state) {
    if(state === 'on') {
        setFanSpeed(255);
    } else {
        setFanSpeed(0);
    }
}

function updateFanSpeed(value) {
    let percent = Math.round((value / 255) * 100);
    document.getElementById('speedDisplay').innerHTML = percent;
}

function setFanSpeed(value) {
    fetch(`/fan?speed=${value}`)
        .then(() => refreshData());
}

// DJ controls
function playNote(freq) {
    fetch(`/buzzer?action=note&freq=${freq}&dur=500`);
}

function stopNote() {
    fetch('/buzzer?action=stop');
}

function playMelody(id) {
    fetch(`/buzzer?action=melody&id=${id}`);
}

function djStop() {
    fetch('/buzzer?action=stop');
}

function updateTempoDisplay(val) {
    document.getElementById('tempoDisplay').innerHTML = val;
}

function setTempo(val) {
    fetch(`/buzzer?action=tempo&bpm=${val}`);
}

setInterval(refreshData, 3000);
window.onload = refreshData;
