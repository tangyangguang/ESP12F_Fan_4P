#include "web/FanWeb.h"

#include "Esp8266BaseWeb.h"
#include "Esp8266BaseWiFi.h"
#include "Esp8266BaseNTP.h"
#include "Esp8266BaseLog.h"
#include "Esp8266BaseConfig.h"

FanController* FanWeb::_controller = nullptr;
IRReceiverDriver* FanWeb::_ir = nullptr;

FanWeb::FanWeb(FanController& controller, IRReceiverDriver& ir) {
    _controller = &controller;
    _ir = &ir;
}

// ----------------------------------------------------------------------------
// FanWeb Implementation using Esp8266BaseWeb (PROGMEM + Chunked)
// ----------------------------------------------------------------------------

// Static HTML parts in PROGMEM
static const char APP_STYLE[] PROGMEM =
    "<style>"
    "body{padding:10px;max-width:560px;font:14px/1.45 -apple-system,BlinkMacSystemFont,'Segoe UI',Arial,sans-serif;color:#1f2937}h2,h3{font-size:14px;font-weight:400;margin:0 0 8px;color:#111827}"
    ".top{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:center;margin-bottom:8px}.muted{color:#6b7280;font-size:14px}"
    ".speed{font-size:38px;font-weight:600;line-height:1;text-align:right;color:#111827}.unit{font-size:16px;font-weight:400;color:#6b7280}"
    ".panel{border:1px solid #d7dee8;border-radius:6px;padding:10px;margin:8px 0;background:#fff}.tight{margin-top:6px}"
    ".stats{display:grid;grid-template-columns:1fr 1fr;gap:6px}.stat{background:#f8fafc;border:1px solid #e8edf3;border-radius:6px;padding:7px 8px;min-width:0}"
    ".stat span{display:block;color:#6b7280;font-size:14px}.stat b{display:block;font-size:14px;font-weight:400;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#111827}"
    ".chips{display:grid;grid-template-columns:repeat(5,1fr);gap:6px}.chips3{grid-template-columns:repeat(3,1fr)}"
    "button,.btn{background:#2563a6;color:#fff;border:0;border-radius:6px;padding:8px 7px;cursor:pointer;text-align:center;text-decoration:none;font-size:14px;font-weight:400;min-height:36px;box-sizing:border-box}"
    "button.secondary,.btn.secondary{background:#6b7280}button.danger{background:#b64a2f}.row{display:grid;grid-template-columns:1fr 76px;gap:7px;align-items:center;margin-top:4px}"
    ".actions{display:grid;grid-template-columns:1fr 1fr;gap:7px;margin-top:9px}.actions button{min-height:40px}"
    ".formgrid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.field{min-width:0}label{display:block;font-size:14px;font-weight:400;color:#374151;margin:0 0 3px}"
    "input,select{width:100%;min-height:38px;box-sizing:border-box;border:1px solid #c8d0da;border-radius:6px;padding:8px;background:#fff;font-size:14px;font-weight:400;margin:0;color:#111827}"
    ".row input:not([type=submit]){height:42px;min-height:42px;margin:0!important;padding:0 10px;display:block}.row button{height:42px;min-height:42px;padding:0 7px;align-self:center}"
    ".oktxt{color:#157347}.errtxt{color:#b42318}.help{color:#6b7280;font-size:14px;margin:3px 0 0;line-height:1.4}"
    ".savebar{display:block;margin-top:8px;padding:7px 8px;border-radius:6px;background:#f8fafc;border:1px solid #e8edf3;font-size:14px}.savebar.oktxt{background:#f0f9f4;border-color:#b7e4c7}.savebar.errtxt{background:#fff5f3;border-color:#f1b8ad}"
    "pre.log{white-space:pre-wrap;word-break:break-word;background:#111827;color:#e5e7eb;border-radius:6px;padding:9px;max-height:430px;overflow:auto;font-size:14px;font-weight:400}"
    "@media(max-width:390px){body{padding:8px}.chips{grid-template-columns:repeat(3,1fr)}.chips3{grid-template-columns:repeat(3,1fr)}.formgrid{grid-template-columns:1fr}}"
    "</style>";

static const char FAN_PAGE_TOP[] PROGMEM =
    "<div class=top><div><h2>Fan</h2><div class=muted>Fast presets, exact input</div></div><div class=speed><span id=tgtTop>";
static const char FAN_SPEED_END[] PROGMEM =
    "</span><span class=unit>%</span></div></div><div class='panel tight'><div class=stats>";
static const char FAN_STATUS_MID[] PROGMEM =
    "</div></div><div class=panel><h3>Speed</h3><div class=chips>"
    "<button onclick='spd(0)'>Off</button><button onclick='spd(25)'>25</button><button onclick='spd(50)'>50</button><button onclick='spd(75)'>75</button><button onclick='spd(100)'>100</button>"
    "</div><label>Custom (%)</label><div class=row><input id=sv type=number min=0 max=100 value='";
static const char FAN_SPEED_INPUT_END[] PROGMEM =
    "'><button onclick='spd(document.getElementById(\"sv\").value)'>Apply</button></div></div>"
    "<div class=panel><h3>Timer</h3><div class='chips chips3'>"
    "<button onclick='tm(30)'>30 min</button><button onclick='tm(60)'>1 h</button><button onclick='tm(120)'>2 h</button>"
    "</div><label>Custom (min)</label><div class=row><input id=tv type=number min=0 max=5940 value='";
static const char FAN_TIMER_INPUT_END[] PROGMEM =
    "'><button onclick='tm(document.getElementById(\"tv\").value)'>Set</button></div>"
    "<div class=actions><button class=secondary onclick='tm(0)'>Cancel timer</button><button class=danger onclick='stopFan()'>Stop fan</button></div>"
    "<div class=help>Cancel timer keeps the fan running. Stop fan turns the fan off and clears the timer.</div></div>"
    "<script>"
    "var rem=";
static const char FAN_SCRIPT_TIMER_MID[] PROGMEM =
    ";var clkMs=0,clkOk=false;function e(i,v){var x=document.getElementById(i);if(x)x.textContent=v}"
    "function pad(n){return(n<10?'0':'')+n}"
    "function cp(s){var m=/(\\d+)-(\\d+)-(\\d+) (\\d+):(\\d+):(\\d+)/.exec(s||'');return m?new Date(+m[1],m[2]-1,+m[3],+m[4],+m[5],+m[6]).getTime():0}"
    "function cf(t){var d=new Date(t);return d.getFullYear()+'-'+pad(d.getMonth()+1)+'-'+pad(d.getDate())+' '+pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds())}"
    "function tf(s){s=parseInt(s||0);if(s<=0)return'Off';var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),r=s%60;return h+'h '+m+'m '+r+'s'}"
    "function draw(d){e('st',d.state);e('tgt',d.target_speed+'%');e('tgtTop',d.target_speed);e('out',d.speed+'%');e('tim',tf(d.timer_remaining));e('run',Math.floor(d.run_duration/3600)+' h');e('ip',d.ip);e('rssi',d.rssi+' dBm');e('clk',d.clock);e('blk',d.blocked?'Yes':'No');document.getElementById('blk').className=d.blocked?'errtxt':'oktxt';rem=d.timer_remaining;clkMs=cp(d.clock);clkOk=clkMs>0;document.getElementById('sv').value=d.target_speed;document.getElementById('tv').value=Math.floor(rem/60)}"
    "function poll(){fetch('/api/status').then(r=>r.json()).then(j=>{if(j.ok)draw(j.data)})}"
    "function post(u,b,cb){fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(()=>{if(cb)cb();setTimeout(poll,250)})}"
    "function spd(v){v=parseInt(v||0);if(v>=0&&v<=100){e('tgt',v+'%');e('tgtTop',v);document.getElementById('sv').value=v;post('/api/speed','speed='+v)}}"
    "function tm(v){v=parseInt(v||0);if(v>=0&&v<=5940){rem=v*60;e('tim',tf(rem));document.getElementById('tv').value=v;post('/api/timer','seconds='+rem)}}"
    "function stopFan(){rem=0;e('tim','Off');e('tgt','0%');e('tgtTop','0');post('/api/stop','')}"
    "function uiTick(){if(rem>0)rem--;e('tim',tf(rem));if(clkOk){clkMs+=1000;e('clk',cf(clkMs))}}"
    "clkMs=cp(document.getElementById('clk').textContent);clkOk=clkMs>0;setInterval(uiTick,1000);setInterval(poll,3000)"
    "</script>";

void FanWeb::handleStatusPage() {
    if (!Esp8266BaseWeb::checkAuth()) return;
    ESP8266BASE_LOG_I("FanWeb", "page_view path=/fan");

    Esp8266BaseWeb::sendHeader();
    Esp8266BaseWeb::sendContent_P(APP_STYLE);
    Esp8266BaseWeb::sendContent_P(FAN_PAGE_TOP);

    char buf[96];

    snprintf(buf, sizeof(buf), "%d", _controller->getTargetSpeed());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(FAN_SPEED_END);
    
    // State
    Esp8266BaseWeb::sendChunk("<div class=stat><span>State</span><b id=st>");
    switch (_controller->getState()) {
        case SYS_IDLE: Esp8266BaseWeb::sendChunk("Idle"); break;
        case SYS_RUNNING: Esp8266BaseWeb::sendChunk("Running"); break;
        case SYS_SLEEP: Esp8266BaseWeb::sendChunk("Sleep"); break;
        case SYS_ERROR: Esp8266BaseWeb::sendChunk("Error"); break;
        default: Esp8266BaseWeb::sendChunk("Unknown"); break;
    }
    Esp8266BaseWeb::sendChunk("</b></div>");
    
    // Speed
    snprintf(buf, sizeof(buf), "<div class=stat><span>Target</span><b id=tgt>%d%%</b></div>", _controller->getTargetSpeed());
    Esp8266BaseWeb::sendChunk(buf);

    snprintf(buf, sizeof(buf), "<div class=stat><span>Output</span><b id=out>%d%%</b></div>", _controller->getCurrentSpeed());
    Esp8266BaseWeb::sendChunk(buf);

    // Timer
    uint32_t timer = _controller->getTimerRemaining();
    if (timer > 0) {
        uint32_t h = timer / 3600, m = (timer % 3600) / 60, s = timer % 60;
        snprintf(buf, sizeof(buf), "%luh %lum %lus", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    } else {
        strcpy(buf, "Off");
    }
    Esp8266BaseWeb::sendChunk("<div class=stat><span>Timer</span><b id=tim>");
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendChunk("</b></div>");

    // Run Time
    snprintf(buf, sizeof(buf), "<div class=stat><span>Run time</span><b id=run>%lu h</b></div>",
             (unsigned long)(_controller->getTotalRunDuration() / 3600));
    Esp8266BaseWeb::sendChunk(buf);

    // WiFi
    const char* ip = "Disconnected";
    long rssi = 0;
    if (Esp8266BaseWiFi::isConnected()) {
        ip = Esp8266BaseWiFi::ip();
        rssi = (long)WiFi.RSSI();
    }
    Esp8266BaseWeb::sendChunk("<div class=stat><span>IP</span><b id=ip>");
    Esp8266BaseWeb::sendChunk(ip);
    Esp8266BaseWeb::sendChunk("</b></div>");
    snprintf(buf, sizeof(buf), "<div class=stat><span>RSSI</span><b id=rssi>%ld dBm</b></div>", rssi);
    Esp8266BaseWeb::sendChunk(buf);

    // NTP
    if (Esp8266BaseNTP::isSynced()) {
        Esp8266BaseNTP::formatTo(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S");
    } else {
        strcpy(buf, "N/A");
    }
    Esp8266BaseWeb::sendChunk("<div class=stat><span>Clock</span><b id=clk>");
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendChunk("</b></div>");

    // Blocked
    snprintf(buf, sizeof(buf), "<div class=stat><span>Blocked</span><b id=blk class=%s>%s</b></div>",
             _controller->isBlocked() ? "errtxt" : "oktxt",
             _controller->isBlocked() ? "Yes" : "No");
    Esp8266BaseWeb::sendChunk(buf);

    Esp8266BaseWeb::sendContent_P(FAN_STATUS_MID);
    snprintf(buf, sizeof(buf), "%d", _controller->getTargetSpeed());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(FAN_SPEED_INPUT_END);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(_controller->getTimerRemaining() / 60));
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(FAN_TIMER_INPUT_END);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)_controller->getTimerRemaining());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(FAN_SCRIPT_TIMER_MID);

    Esp8266BaseWeb::sendFooter();
}

// Config Page HTML parts
static const char CONFIG_PAGE_TOP[] PROGMEM = 
    "<div class=top><div><h2>Settings</h2><div class=muted>Fan behavior and IR learning</div></div><a class='btn secondary' href='/auth'>Password</a></div>"
    "<form class=panel onsubmit='saveCfg(this);return false'>"
    "<h3>Fan behavior</h3><div class=formgrid><div class=field><label>Min speed (%)</label><input type=number name=min_speed min=0 max=50 value='";
static const char CONFIG_MIN_END[] PROGMEM = "'><div class=help>Low commands rise to this value.</div></div>"
    "<div class=field><label>Sleep wait (s)</label><input type=number name=sleep_wait min=0 max=3600 value='";
static const char CONFIG_SLEEP_END[] PROGMEM = "'><div class=help>Stopped this long before modem sleep.</div></div>"
    "<div class=field><label>Soft start (ms)</label><input type=number name=soft_start min=0 max=10000 value='";
static const char CONFIG_START_END[] PROGMEM = "'><div class=help>Ramp up time.</div></div>"
    "<div class=field><label>Soft stop (ms)</label><input type=number name=soft_stop min=0 max=10000 value='";
static const char CONFIG_STOP_END[] PROGMEM = "'><div class=help>Ramp down time.</div></div>"
    "<div class=field><label>Block detect (ms)</label><input type=number name=block_detect min=100 max=5000 value='";
static const char CONFIG_BLOCK_END[] PROGMEM = "'><div class=help>No RPM for this long means blocked.</div></div>"
    "<div class=field><label>Power-on restore</label><select name=auto_restore><option value=1 ";
static const char CONFIG_AUTO_END[] PROGMEM = ">Enabled</option><option value=0 ";
static const char CONFIG_AUTO_END2[] PROGMEM = ">Disabled</option></select><div class=help>Restore last speed and timer after reboot.</div></div></div>"
    "<button id=saveBtn type=submit>Save</button><span id=saveMsg class='savebar muted'>Ready</span></form>"
    "<div class=panel><h3>IR learning</h3><div class='chips chips3'>"
    "<button onclick='learn(0,\"Speed Up\")'>Speed Up</button><button onclick='learn(1,\"Speed Down\")'>Speed Down</button><button onclick='learn(2,\"Stop\")'>Stop</button>"
    "<button onclick='learn(3,\"30 min\")'>30 min</button><button onclick='learn(4,\"1 h\")'>1 h</button><button onclick='learn(5,\"2 h\")'>2 h</button>"
    "</div><div class=help>Press one, then point the remote within 10 seconds.</div><span id=irMsg class='savebar muted'>Ready</span></div>"
    "<script>"
    "function setMsg(t,c){var m=document.getElementById('saveMsg');m.textContent=t;m.className='savebar '+c}"
    "function setIr(t,c){var m=document.getElementById('irMsg');m.textContent=t;m.className='savebar '+c}"
    "function applyCfg(d,f){if(!d)return;f.min_speed.value=d.min_effective_speed;f.sleep_wait.value=d.sleep_wait;f.soft_start.value=d.soft_start;f.soft_stop.value=d.soft_stop;f.block_detect.value=d.block_detect;f.auto_restore.value=d.auto_restore?1:0}"
    "function saveCfg(f){var b=document.getElementById('saveBtn');b.disabled=true;b.textContent='Saving';setMsg('Saving...','muted');fetch('/api/config',{method:'POST',body:new URLSearchParams(new FormData(f))}).then(r=>r.json().then(j=>({ok:r.ok,j:j}))).then(x=>{b.disabled=false;b.textContent='Save';if(x.ok&&x.j.ok){applyCfg(x.j.data,f);var n=x.j.changed||0;setMsg('Saved - '+(n?n+' changed':'no changes')+' - '+new Date().toLocaleTimeString(),'oktxt')}else{setMsg('Save failed','errtxt')}}).catch(()=>{b.disabled=false;b.textContent='Save';setMsg('Save failed: network error','errtxt')})}"
    "function watchIr(n,seq){fetch('/api/status').then(r=>r.json()).then(j=>{var d=j.data;if(!d)return;if(d.ir_learning){setIr('Learning '+n+' - '+d.ir_remaining+'s','muted');setTimeout(()=>watchIr(n,seq),500)}else if(d.ir_learn_seq!=seq){setIr('Learned '+n+' - protocol '+d.ir_last_protocol+' code '+d.ir_last_code,'oktxt')}else{setIr('Learn timeout - no valid signal','errtxt')}}).catch(()=>setIr('Learn status failed','errtxt'))}"
    "function learn(i,n){setIr('Starting '+n+'...','muted');fetch('/api/ir/learn',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key_index='+i}).then(r=>r.json()).then(d=>{if(d.ok){setIr('Learning '+n+' - press remote','muted');watchIr(n,d.seq)}else setIr('Learn failed','errtxt')}).catch(()=>setIr('Learn failed: network error','errtxt'))}"
    "</script>";

void FanWeb::handleConfigPage() {
    if (!Esp8266BaseWeb::checkAuth()) return;
    ESP8266BASE_LOG_I("FanWeb", "page_view path=/config");

    Esp8266BaseWeb::sendHeader();
    Esp8266BaseWeb::sendContent_P(APP_STYLE);
    Esp8266BaseWeb::sendContent_P(CONFIG_PAGE_TOP);

    char buf[32];
    
    // Min Speed
    snprintf(buf, sizeof(buf), "%d", _controller->getMinEffectiveSpeed());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(CONFIG_MIN_END);

    // Sleep Wait
    snprintf(buf, sizeof(buf), "%d", _controller->getSleepWaitTime());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(CONFIG_SLEEP_END);

    // Soft Start
    snprintf(buf, sizeof(buf), "%d", _controller->getSoftStartTime());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(CONFIG_START_END);

    // Soft Stop
    snprintf(buf, sizeof(buf), "%d", _controller->getSoftStopTime());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(CONFIG_STOP_END);

    // Block Detect
    snprintf(buf, sizeof(buf), "%d", _controller->getBlockDetectTime());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(CONFIG_BLOCK_END);

    // Auto Restore
    Esp8266BaseWeb::sendContent_P(_controller->getAutoRestore() ? "selected" : "");
    Esp8266BaseWeb::sendContent_P(CONFIG_AUTO_END);
    Esp8266BaseWeb::sendContent_P(_controller->getAutoRestore() ? "" : "selected");
    Esp8266BaseWeb::sendContent_P(CONFIG_AUTO_END2);

    Esp8266BaseWeb::sendFooter();
}

// API Handlers

void FanWeb::handleApiStatus() {
    if (!Esp8266BaseWeb::checkAuth()) return;
    
    char buf[640];
    char clock[24];
    const char* stateStr;
    switch (_controller->getState()) {
        case SYS_IDLE: stateStr = "Idle"; break;
        case SYS_RUNNING: stateStr = "Running"; break;
        case SYS_SLEEP: stateStr = "Sleep"; break;
        case SYS_ERROR: stateStr = "Error"; break;
        default: stateStr = "Unknown"; break;
    }
    
    const char* ip = Esp8266BaseWiFi::isConnected() ? Esp8266BaseWiFi::ip() : "N/A";
    long rssi = Esp8266BaseWiFi::isConnected() ? (long)WiFi.RSSI() : 0;
    if (Esp8266BaseNTP::isSynced()) {
        Esp8266BaseNTP::formatTo(clock, sizeof(clock), "%Y-%m-%d %H:%M:%S");
    } else {
        strcpy(clock, "N/A");
    }
    
    uint8_t ir_key = _ir->getLearnedKeyIndex();
    snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"data\":{\"state\":\"%s\",\"speed\":%d,\"target_speed\":%d,\"timer_remaining\":%lu,"
        "\"run_duration\":%lu,\"blocked\":%s,\"ip\":\"%s\",\"rssi\":%ld,\"clock\":\"%s\","
        "\"ir_learning\":%s,\"ir_key\":%u,\"ir_remaining\":%lu,\"ir_learn_seq\":%lu,"
        "\"ir_last_protocol\":%u,\"ir_last_code\":\"0x%08llX\"}}",
        stateStr, _controller->getCurrentSpeed(), _controller->getTargetSpeed(),
        (unsigned long)_controller->getTimerRemaining(),
        (unsigned long)_controller->getTotalRunDuration(),
        _controller->isBlocked() ? "true" : "false", ip, rssi, clock,
        _ir->isLearning() ? "true" : "false", ir_key,
        (unsigned long)_ir->getLearningRemaining(),
        (unsigned long)_ir->getLearnedSequence(),
        _ir->getLastProtocol(),
        (unsigned long long)_ir->getLastCode()
    );
    Esp8266BaseWeb::server().send(200, "application/json", buf);
}

void FanWeb::handleApiSpeed() {
    if (!Esp8266BaseWeb::checkAuth()) return;
    
    auto& server = Esp8266BaseWeb::server();
    if (server.method() == HTTP_POST) {
        String speedStr = server.arg("speed");
        if (speedStr.length() > 0) {
            int speed = speedStr.toInt();
            if (speed >= 0 && speed <= 100) {
                ESP8266BASE_LOG_I("FanWeb", "user_action speed=%d", speed);
                _controller->setSpeed(speed);
                char buf[64];
                snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%d,\"target_speed\":%d}",
                         _controller->getCurrentSpeed(), _controller->getTargetSpeed());
                server.send(200, "application/json", buf);
                return;
            }
        }
        ESP8266BASE_LOG_W("FanWeb", "invalid_speed_request value=%s", speedStr.c_str());
        server.send(400, "application/json", "{\"error\":\"invalid request\"}");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%d,\"target_speed\":%d}",
                 _controller->getCurrentSpeed(), _controller->getTargetSpeed());
        server.send(200, "application/json", buf);
    }
}

void FanWeb::handleApiTimer() {
    if (!Esp8266BaseWeb::checkAuth()) return;

    auto& server = Esp8266BaseWeb::server();
    if (server.method() == HTTP_POST) {
        String secStr = server.arg("seconds");
        if (secStr.length() > 0) {
            unsigned long seconds = secStr.toInt();
            if (seconds <= 356400) {
                ESP8266BASE_LOG_I("FanWeb", "user_action timer_seconds=%lu", seconds);
                _controller->setTimer(seconds);
                char buf[64];
                snprintf(buf, sizeof(buf), "{\"ok\":true,\"timer_remaining\":%lu}",
                         (unsigned long)_controller->getTimerRemaining());
                server.send(200, "application/json", buf);
                return;
            }
        }
        ESP8266BASE_LOG_W("FanWeb", "invalid_timer_request seconds=%s", secStr.c_str());
        server.send(400, "application/json", "{\"error\":\"invalid request\"}");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"ok\":true,\"timer_remaining\":%lu}", (unsigned long)_controller->getTimerRemaining());
        server.send(200, "application/json", buf);
    }
}

void FanWeb::handleApiStop() {
    if (!Esp8266BaseWeb::checkAuth()) return;
    ESP8266BASE_LOG_I("FanWeb", "user_action stop_fan");
    _controller->stop();
    Esp8266BaseWeb::server().send(200, "application/json", "{\"ok\":true}");
}

void FanWeb::handleApiConfig() {
    if (!Esp8266BaseWeb::checkAuth()) return;

    auto& server = Esp8266BaseWeb::server();
    if (server.method() == HTTP_POST) {
        ESP8266BASE_LOG_I("FanWeb", "user_action save_config");
        uint8_t changed = 0;
        // Parse args
        if (server.hasArg("min_speed")) {
            uint8_t v = server.arg("min_speed").toInt();
            uint8_t old = _controller->getMinEffectiveSpeed();
            if (v != old) {
                _controller->setMinEffectiveSpeed(v);
                changed++;
            }
        }
        if (server.hasArg("soft_start")) {
            uint16_t v = server.arg("soft_start").toInt();
            uint16_t old = _controller->getSoftStartTime();
            if (v != old) {
                _controller->setSoftStartTime(v);
                changed++;
            }
        }
        if (server.hasArg("soft_stop")) {
            uint16_t v = server.arg("soft_stop").toInt();
            uint16_t old = _controller->getSoftStopTime();
            if (v != old) {
                _controller->setSoftStopTime(v);
                changed++;
            }
        }
        if (server.hasArg("block_detect")) {
            uint16_t v = server.arg("block_detect").toInt();
            uint16_t old = _controller->getBlockDetectTime();
            if (v != old) {
                _controller->setBlockDetectTime(v);
                changed++;
            }
        }
        if (server.hasArg("sleep_wait")) {
            uint16_t v = server.arg("sleep_wait").toInt();
            uint16_t old = _controller->getSleepWaitTime();
            if (v != old) {
                _controller->setSleepWaitTime(v);
                changed++;
            }
        }
        if (server.hasArg("auto_restore")) {
            bool v = server.arg("auto_restore").toInt() != 0;
            bool old = _controller->getAutoRestore();
            if (v != old) {
                _controller->setAutoRestore(v);
                changed++;
            }
        }
        bool flushed = Esp8266BaseConfig::flush();
        ESP8266BASE_LOG_I("FanWeb", "config_save_complete changed=%u flushed=%u",
                          (unsigned)changed, flushed ? 1U : 0U);
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"changed\":%u,\"flushed\":%s,\"data\":{\"min_effective_speed\":%d,\"soft_start\":%d,\"soft_stop\":%d,"
            "\"block_detect\":%d,\"sleep_wait\":%d,\"auto_restore\":%s}}",
            (unsigned)changed,
            flushed ? "true" : "false",
            _controller->getMinEffectiveSpeed(),
            _controller->getSoftStartTime(),
            _controller->getSoftStopTime(),
            _controller->getBlockDetectTime(),
            _controller->getSleepWaitTime(),
            _controller->getAutoRestore() ? "true" : "false"
        );
        server.send(200, "application/json", buf);
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"data\":{\"min_effective_speed\":%d,\"soft_start\":%d,\"soft_stop\":%d,"
            "\"block_detect\":%d,\"sleep_wait\":%d,\"auto_restore\":%s}}",
            _controller->getMinEffectiveSpeed(),
            _controller->getSoftStartTime(),
            _controller->getSoftStopTime(),
            _controller->getBlockDetectTime(),
            _controller->getSleepWaitTime(),
            _controller->getAutoRestore() ? "true" : "false"
        );
        server.send(200, "application/json", buf);
    }
}

void FanWeb::handleApiLogs() {
    if (!Esp8266BaseWeb::checkAuth()) return;
    Esp8266BaseWeb::server().send(200, "application/json", "{\"ok\":true,\"data\":[]}");
}

void FanWeb::handleApiReset() {
    if (!Esp8266BaseWeb::checkAuth()) return;
    ESP8266BASE_LOG_W("FanWeb", "user_action factory_reset");
    _controller->resetFactory();
    // Note: This will restart, so response might not be sent.
    Esp8266BaseWeb::server().send(200, "application/json", "{\"ok\":true}");
}

void FanWeb::handleApiIrLearn() {
    if (!Esp8266BaseWeb::checkAuth()) return;

    auto& server = Esp8266BaseWeb::server();
    if (server.method() == HTTP_POST && server.hasArg("key_index")) {
        int idx = server.arg("key_index").toInt();
        if (idx >= 0 && idx < 6 && _ir->startLearning(idx)) {
            ESP8266BASE_LOG_I("FanWeb", "user_action ir_learn key=%d", idx);
            char buf[96];
            snprintf(buf, sizeof(buf), "{\"ok\":true,\"learning\":true,\"timeout\":10,\"seq\":%lu}",
                     (unsigned long)_ir->getLearnedSequence());
            server.send(200, "application/json", buf);
            return;
        }
    }
    ESP8266BASE_LOG_W("FanWeb", "invalid_ir_learn_request");
    server.send(400, "application/json", "{\"error\":\"invalid request\"}");
}

void FanWeb::handleApiIrStatus() {
    if (!Esp8266BaseWeb::checkAuth()) return;

    char buf[512];
    char* p = buf;
    p += snprintf(p, sizeof(buf), "{\"ok\":true,\"learning\":%s,\"codes\":[", _ir->isLearning() ? "true" : "false");

    for (uint8_t i = 0; i < 6; i++) {
        uint8_t proto;
        uint64_t code;
        _ir->getKeyCode(i, &proto, &code);
        p += snprintf(p, buf + sizeof(buf) - p, "{\"protocol\":%d,\"code\":\"0x%08llX\"}%s",
                 proto, (unsigned long long)code, i < 5 ? "," : "");
    }
    snprintf(p, buf + sizeof(buf) - p, "]}");
    
    Esp8266BaseWeb::server().send(200, "application/json", buf);
}
