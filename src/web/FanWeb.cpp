#include "web/FanWeb.h"

#include "Esp8266BaseWeb.h"
#include "Esp8266BaseWiFi.h"
#include "Esp8266BaseNTP.h"
#include "Esp8266BaseLog.h"
#include "Esp8266BaseConfig.h"

FanController* FanWeb::_controller = nullptr;
IRReceiverDriver* FanWeb::_ir = nullptr;

namespace {
bool parseUintArg(const String& value, uint32_t min_value, uint32_t max_value, uint32_t* out) {
    if (out == nullptr || value.length() == 0) return false;

    uint32_t parsed = 0;
    for (unsigned int i = 0; i < value.length(); i++) {
        char c = value.c_str()[i];
        if (c < '0' || c > '9') return false;
        uint32_t digit = static_cast<uint32_t>(c - '0');
        if (parsed > (max_value - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    if (parsed < min_value || parsed > max_value) return false;
    *out = parsed;
    return true;
}

void formatRunDuration(uint32_t seconds, char* out, size_t out_size) {
    if (out == nullptr || out_size == 0) return;
    if (seconds < 3600) {
        snprintf(out, out_size, "%lum %lus",
                 static_cast<unsigned long>(seconds / 60),
                 static_cast<unsigned long>(seconds % 60));
    } else {
        snprintf(out, out_size, "%luh %lum",
                 static_cast<unsigned long>(seconds / 3600),
                 static_cast<unsigned long>((seconds % 3600) / 60));
    }
}

const char* irKeyName(uint8_t index) {
    switch (index) {
        case IR_KEY_SPEED_UP: return "Speed Up";
        case IR_KEY_SPEED_DOWN: return "Speed Down";
        case IR_KEY_STOP: return "Stop";
        case IR_KEY_TIMER_30M: return "30 min";
        case IR_KEY_TIMER_1H: return "1 h";
        case IR_KEY_TIMER_2H: return "2 h";
        case IR_KEY_TIMER_4H: return "4 h";
        case IR_KEY_TIMER_8H: return "8 h";
        default: return "Unknown";
    }
}
}

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
    ".speed{font-size:38px;font-weight:600;line-height:1;text-align:right;color:#111827}.unit{font-size:16px;font-weight:400;color:#6b7280}.targetline{margin-top:4px;text-align:right;color:#6b7280;font-size:14px;font-weight:400}"
    ".panel{border:1px solid #d7dee8;border-radius:6px;padding:10px;margin:8px 0;background:#fff}.tight{margin-top:6px}"
    ".stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(118px,1fr));gap:6px}.stat{background:#f8fafc;border:1px solid #e8edf3;border-radius:6px;padding:7px 8px;min-width:0}.stat.state{grid-column:1/-1}"
    ".stat span{display:block;color:#6b7280;font-size:14px}.stat b{display:block;font-size:14px;font-weight:400;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#111827}"
    ".chips{display:grid;grid-template-columns:repeat(5,1fr);gap:6px}"
    "button,.btn{background:#2563a6;color:#fff;border:0;border-radius:6px;padding:8px 7px;cursor:pointer;text-align:center;text-decoration:none;font-size:14px;font-weight:400;min-height:36px;box-sizing:border-box}"
    "button.secondary,.btn.secondary{background:#6b7280}button.danger{background:#b64a2f}.row{display:grid;grid-template-columns:1fr 76px;gap:7px;align-items:center;margin-top:4px}"
    ".actions{display:grid;grid-template-columns:1fr 1fr;gap:7px;margin-top:14px}.actions button{min-height:40px}"
    ".formgrid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.field{min-width:0}label{display:block;font-size:14px;font-weight:400;color:#374151;margin:0 0 3px}"
    "input,select{width:100%;min-height:38px;box-sizing:border-box;border:1px solid #c8d0da;border-radius:6px;padding:8px;background:#fff;font-size:14px;font-weight:400;margin:0;color:#111827}"
    ".row input:not([type=submit]){height:42px;min-height:42px;margin:0!important;padding:0 10px;display:block}.row button{height:42px;min-height:42px;padding:0 7px;align-self:center}"
    ".oktxt{color:#157347}.errtxt{color:#b42318}.help{color:#6b7280;font-size:14px;margin:7px 0 0;line-height:1.4}"
    ".irlist{display:grid;gap:5px}.irrow{display:grid;grid-template-columns:1fr 58px 58px;gap:6px;align-items:center;background:#f8fafc;border:1px solid #e8edf3;border-radius:6px;padding:6px 8px;transition:background-color .2s,border-color .2s}.irrow.hit{background:#fff7ed;border-color:#fdba74}.irrow b{display:block;font-size:14px;font-weight:400;color:#111827}.irrow span{display:block;color:#6b7280;font-size:13px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.irrow span.warn{color:#b45309}.irrow button{min-height:32px;padding:0 6px}.irrow button.secondary{background:#6b7280}"
    ".savebar{display:block;margin-top:8px;padding:7px 8px;border-radius:6px;background:#f8fafc;border:1px solid #e8edf3;font-size:14px}.savebar.oktxt{background:#f0f9f4;border-color:#b7e4c7}.savebar.errtxt{background:#fff5f3;border-color:#f1b8ad}"
    "pre.log{white-space:pre-wrap;word-break:break-word;background:#111827;color:#e5e7eb;border-radius:6px;padding:9px;max-height:430px;overflow:auto;font-size:14px;font-weight:400}"
    "@media(max-width:390px){body{padding:8px}.chips{grid-template-columns:repeat(3,1fr)}.formgrid{grid-template-columns:1fr}}"
    "</style>";

static const char FAN_PAGE_TOP[] PROGMEM =
    "<div class=top><div><h2>Fan</h2><div class=muted>Fast presets, exact input</div></div><div><div class=speed><span id=outTop>";
static const char FAN_SPEED_END[] PROGMEM =
    "</span><span class=unit>%</span></div><div class=targetline><span id=rpmTop>";
static const char FAN_RPM_END[] PROGMEM =
    "</span><span id=targetTopWrap";
static const char FAN_TARGET_START[] PROGMEM =
    "> &middot; Target <span id=tgtTop>";
static const char FAN_TARGET_WRAP_END[] PROGMEM =
    "</span>%</span></div></div></div><div class='panel tight'><div class=stats>";
static const char FAN_STATUS_MID[] PROGMEM =
    "</div></div><div class=panel><h3>Speed</h3><div class=chips>"
    "<button onclick='spd(0)'>Off</button><button onclick='spd(25)'>25</button><button onclick='spd(50)'>50</button><button onclick='spd(75)'>75</button><button onclick='spd(100)'>100</button>"
    "</div><label>Custom (%)</label><div class=row><input id=sv type=number min=0 max=100 value='";
static const char FAN_SPEED_INPUT_END[] PROGMEM =
    "'><button onclick='spd(document.getElementById(\"sv\").value)'>Apply</button></div></div>"
    "<div class=panel><h3>Timer</h3><div class=chips>"
    "<button onclick='tm(30)'>30 min</button><button onclick='tm(60)'>1 h</button><button onclick='tm(120)'>2 h</button><button onclick='tm(240)'>4 h</button><button onclick='tm(480)'>8 h</button>"
    "</div><label>Custom (min)</label><div class=row><input id=tv type=number min=0 max=5940 value='";
static const char FAN_TIMER_INPUT_END[] PROGMEM =
    "'><button onclick='tm(document.getElementById(\"tv\").value)'>Set</button></div>"
    "<div class=actions><button class=secondary onclick='tm(0)'>Cancel timer</button><button class=danger onclick='stopFan()'>Stop fan</button></div>"
    "<div class=help>Cancel timer keeps the fan running. Stop fan turns the fan off and clears the timer.</div></div>"
    "<script>"
    "var rem=";
static const char FAN_SCRIPT_TIMER_MID[] PROGMEM =
    ";function e(i,v){var x=document.getElementById(i);if(x)x.textContent=v}"
    "function tf(s){s=parseInt(s||0);if(s<=0)return'Off';var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),r=s%60;return h+'h '+m+'m '+r+'s'}"
    "function rf(s){s=parseInt(s||0);if(s<3600)return Math.floor(s/60)+'m '+(s%60)+'s';return Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m'}"
    "function tt(o,t){var w=document.getElementById('targetTopWrap');if(w)w.style.display=o==t?'none':''}"
    "var pollMs=3000,pollTimer=0;function draw(d){var st=d.blocked?(d.state=='Error'?'Error / Blocked':'Blocked'):d.state;e('st',st);document.getElementById('st').className=d.blocked?'errtxt':'';e('tgt',d.target_speed+'%');e('tgtTop',d.target_speed);e('out',d.speed+'%');e('outTop',d.speed);e('rpmTop',(d.rpm||0)+' rpm');tt(d.speed,d.target_speed);e('gear',d.gear);e('rpm',(d.rpm||0)+' rpm');e('tim',tf(d.timer_remaining));e('runTotal',rf(d.run_duration));e('runBoot',rf(d.boot_run_duration));e('rssi',d.rssi+' dBm');rem=d.timer_remaining;pollMs=d.speed==d.target_speed?3000:500;document.getElementById('sv').value=d.target_speed;document.getElementById('tv').value=Math.floor(rem/60)}"
    "function sched(ms){clearTimeout(pollTimer);pollTimer=setTimeout(poll,ms)}function poll(){fetch('/api/status').then(r=>r.json()).then(j=>{if(j.ok)draw(j.data);sched(pollMs)}).catch(()=>sched(3000))}"
    "function post(u,b,cb){fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(()=>{if(cb)cb();setTimeout(poll,250)})}"
    "function spd(v){v=parseInt(v||0);if(v>=0&&v<=100){e('tgt',v+'%');e('tgtTop',v);e('rpmTop','-- rpm');e('rpm','-- rpm');tt(parseInt(document.getElementById('outTop').textContent||0),v);document.getElementById('sv').value=v;post('/api/speed','speed='+v)}}"
    "function tm(v){v=parseInt(v||0);if(v>=0&&v<=5940){rem=v*60;e('tim',tf(rem));document.getElementById('tv').value=v;post('/api/timer','seconds='+rem)}}"
    "function stopFan(){rem=0;e('tim','Off');e('tgt','0%');e('tgtTop','0');e('outTop','0');e('rpmTop','-- rpm');e('rpm','-- rpm');tt(0,0);post('/api/stop','')}"
    "function uiTick(){if(rem>0)rem--;e('tim',tf(rem))}"
    "setInterval(uiTick,1000);sched(3000)"
    "</script>";

void FanWeb::handleStatusPage() {
    if (!Esp8266BaseWeb::checkAuth()) return;
    ESP8266BASE_LOG_I("FanWeb", "page_view path=/fan");

    Esp8266BaseWeb::sendHeader();
    Esp8266BaseWeb::sendContent_P(APP_STYLE);
    Esp8266BaseWeb::sendContent_P(FAN_PAGE_TOP);

    char buf[160];

    snprintf(buf, sizeof(buf), "%d", _controller->getCurrentSpeed());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(FAN_SPEED_END);
    snprintf(buf, sizeof(buf), "%u rpm", _controller->getCurrentRpm());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(FAN_RPM_END);
    if (_controller->getCurrentSpeed() == _controller->getTargetSpeed()) {
        Esp8266BaseWeb::sendChunk(" style='display:none'");
    }
    Esp8266BaseWeb::sendContent_P(FAN_TARGET_START);
    snprintf(buf, sizeof(buf), "%d", _controller->getTargetSpeed());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(FAN_TARGET_WRAP_END);
    
    // State
    const bool blocked = _controller->isBlocked();
    const char* stateText = "Unknown";
    switch (_controller->getState()) {
        case SYS_IDLE: stateText = "Idle"; break;
        case SYS_RUNNING: stateText = "Running"; break;
        case SYS_SLEEP: stateText = "Sleep"; break;
        case SYS_ERROR: stateText = blocked ? "Error / Blocked" : "Error"; break;
        default: break;
    }
    if (blocked && _controller->getState() != SYS_ERROR) {
        stateText = "Blocked";
    }
    snprintf(buf, sizeof(buf), "<div class='stat state'><span>State</span><b id=st class=%s>%s</b></div>",
             blocked ? "errtxt" : "", stateText);
    Esp8266BaseWeb::sendChunk(buf);
    
    // Speed
    snprintf(buf, sizeof(buf), "<div class=stat><span>Target</span><b id=tgt>%d%%</b></div>", _controller->getTargetSpeed());
    Esp8266BaseWeb::sendChunk(buf);

    snprintf(buf, sizeof(buf), "<div class=stat><span>Output</span><b id=out>%d%%</b></div>", _controller->getCurrentSpeed());
    Esp8266BaseWeb::sendChunk(buf);

    snprintf(buf, sizeof(buf), "<div class=stat><span>Gear</span><b id=gear>%u</b></div>", _controller->getCurrentGear());
    Esp8266BaseWeb::sendChunk(buf);

    snprintf(buf, sizeof(buf), "<div class=stat><span>RPM</span><b id=rpm>%u rpm</b></div>", _controller->getCurrentRpm());
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
    char duration[24];
    formatRunDuration(_controller->getTotalRunDuration(), duration, sizeof(duration));
    snprintf(buf, sizeof(buf), "<div class=stat><span>Total run</span><b id=runTotal>%s</b></div>", duration);
    Esp8266BaseWeb::sendChunk(buf);
    formatRunDuration(_controller->getBootRunDuration(), duration, sizeof(duration));
    snprintf(buf, sizeof(buf), "<div class=stat><span>Boot run</span><b id=runBoot>%s</b></div>", duration);
    Esp8266BaseWeb::sendChunk(buf);

    long rssi = Esp8266BaseWiFi::isConnected() ? (long)WiFi.RSSI() : 0;
    snprintf(buf, sizeof(buf), "<div class=stat><span>RSSI</span><b id=rssi>%ld dBm</b></div>", rssi);
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
    "<div class=field><label>LED flash (ms)</label><input type=number name=led_flash_ms min=0 max=2000 value='";
static const char CONFIG_LED_FLASH_END[] PROGMEM = "'><div class=help>0 disables action feedback flash.</div></div>"
    "<div class=field><label>Power-on restore</label><select name=auto_restore><option value=1 ";
static const char CONFIG_AUTO_END[] PROGMEM = ">Enabled</option><option value=0 ";
static const char CONFIG_AUTO_END2[] PROGMEM = ">Disabled</option></select><div class=help>Restore last speed and timer after reboot.</div></div></div>"
    "<button id=saveBtn type=submit>Save</button><span id=saveMsg class='savebar muted'>Ready</span></form>"
    "<div class=panel><h3>IR learning</h3><div class=irlist>";
static const char CONFIG_IR_END[] PROGMEM =
    "</div><div class=help>Press one, then point the remote within 10 seconds.</div><span id=irMsg class='savebar muted'>Ready</span></div>"
    "<script>"
    "var irNames=['Speed Up','Speed Down','Stop','30 min','1 h','2 h','4 h','8 h'],irToken=0,irDeadline=0,irDup=-1,irReject=0;function irName(i){return irNames[i]||('key '+i)}function irLeft(){return Math.max(0,Math.ceil((irDeadline-Date.now())/1000))}"
    "function setMsg(t,c){var m=document.getElementById('saveMsg');m.textContent=t;m.className='savebar '+c}"
    "function setIr(t,c){var m=document.getElementById('irMsg');m.textContent=t;m.className='savebar '+c}"
    "function setIrRow(i,t,c){var e=document.getElementById('irv'+i);if(e){e.textContent=t;e.className=c||''}}"
    "function hitIr(i){var e=document.getElementById('irr'+i);if(e){e.className='irrow hit';setTimeout(()=>{e.className='irrow'},600)}}"
    "function applyCfg(d,f){if(!d)return;f.min_speed.value=d.min_effective_speed;f.sleep_wait.value=d.sleep_wait;f.soft_start.value=d.soft_start;f.soft_stop.value=d.soft_stop;f.block_detect.value=d.block_detect;f.led_flash_ms.value=d.led_flash_ms;f.auto_restore.value=d.auto_restore?1:0}"
    "function reloadCfg(f){fetch('/api/config').then(r=>r.json()).then(j=>{if(j.ok)applyCfg(j.data,f)})}"
    "function saveCfg(f){var b=document.getElementById('saveBtn');b.disabled=true;b.textContent='Saving';setMsg('Saving...','muted');fetch('/api/config',{method:'POST',body:new URLSearchParams(new FormData(f))}).then(r=>r.json().then(j=>({ok:r.ok,j:j}))).then(x=>{b.disabled=false;b.textContent='Save';if(x.ok&&x.j.ok){applyCfg(x.j.data,f);var n=x.j.changed||0;setMsg('Saved - '+(n?n+' changed':'no changes')+' - '+new Date().toLocaleTimeString(),'oktxt')}else{reloadCfg(f);setMsg(x.j&&x.j.error?x.j.error:'Save failed','errtxt')}}).catch(()=>{b.disabled=false;b.textContent='Save';setMsg('Save failed: network error','errtxt')})}"
    "function finishIr(i,n,seq,tok){fetch('/api/status').then(r=>r.json()).then(j=>{if(tok!=irToken)return;var d=j.data;if(d&&d.ir_learn_seq!=seq){var v='Protocol '+d.ir_last_protocol+' - '+d.ir_last_code;setIrRow(i,v,'');setIr('Learned '+n+' - '+v,'oktxt');setTimeout(()=>location.reload(),600)}else setIr('Learn timeout - no valid signal','errtxt')}).catch(()=>setIr('Learn timeout - no valid signal','errtxt'))}"
    "function showLearn(n){var l=irLeft();if(l<=0)return false;if(irDup>=0)setIr('Already assigned to '+irName(irDup)+'. Press a different key - '+l+'s','errtxt');else setIr('Learning '+n+' - '+l+'s','muted');return true}"
    "function watchIr(i,n,seq,tok){if(tok!=irToken)return;if(!showLearn(n)){finishIr(i,n,seq,tok);return}fetch('/api/status').then(r=>r.json()).then(j=>{if(tok!=irToken)return;var d=j.data;if(!d)return;if(d.ir_learn_seq!=seq){var v='Protocol '+d.ir_last_protocol+' - '+d.ir_last_code;setIrRow(i,v,'');setIr('Learned '+n+' - '+v,'oktxt');setTimeout(()=>location.reload(),600);return}if(!d.ir_learning){setIr('Learn timeout - no valid signal','errtxt');return}var nd=d.ir_duplicate_key<irNames.length?d.ir_duplicate_key:-1;if(nd>=0&&(nd!=irDup||d.ir_reject_seq!=irReject))hitIr(nd);irDup=nd;irReject=d.ir_reject_seq;setTimeout(()=>watchIr(i,n,seq,tok),500)}).catch(()=>{if(tok==irToken&&showLearn(n))setTimeout(()=>watchIr(i,n,seq,tok),500)})}"
    "function learn(i,n){irToken++;irDup=-1;irReject=0;var tok=irToken;setIr('Starting '+n+'...','muted');fetch('/api/ir/learn',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key_index='+i}).then(r=>r.json()).then(d=>{if(d.ok){irReject=d.rej_seq||0;irDeadline=Date.now()+d.timeout*1000;watchIr(i,n,d.seq,tok)}else setIr('Learn failed','errtxt')}).catch(()=>setIr('Learn failed: network error','errtxt'))}"
    "function clearIr(i,n){if(!confirm('Clear IR code for '+n+'?'))return;setIr('Clearing '+n+'...','muted');fetch('/api/ir/learn',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key_index='+i+'&clear=1'}).then(r=>r.json().then(j=>({ok:r.ok,j:j}))).then(x=>{if(x.ok&&x.j.ok){setIrRow(i,'Not learned','');setIr(x.j.changed?('Cleared '+n):('No code for '+n),x.j.changed?'oktxt':'muted');setTimeout(()=>location.reload(),600)}else setIr('Clear failed','errtxt')}).catch(()=>setIr('Clear failed: network error','errtxt'))}"
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

    // LED Flash
    snprintf(buf, sizeof(buf), "%d", _controller->getLedFlashDuration());
    Esp8266BaseWeb::sendChunk(buf);
    Esp8266BaseWeb::sendContent_P(CONFIG_LED_FLASH_END);

    // Auto Restore
    Esp8266BaseWeb::sendChunk(_controller->getAutoRestore() ? "selected" : "");
    Esp8266BaseWeb::sendContent_P(CONFIG_AUTO_END);
    Esp8266BaseWeb::sendChunk(_controller->getAutoRestore() ? "" : "selected");
    Esp8266BaseWeb::sendContent_P(CONFIG_AUTO_END2);

    for (uint8_t i = 0; i < IR_KEY_COUNT; i++) {
        uint8_t protocol = 0;
        uint64_t code = 0;
        _ir->getKeyCode(i, &protocol, &code);
        bool duplicate = false;
        if (protocol != 0 || code != 0) {
            for (uint8_t j = 0; j < IR_KEY_COUNT; j++) {
                if (j == i) continue;
                uint8_t other_protocol = 0;
                uint64_t other_code = 0;
                _ir->getKeyCode(j, &other_protocol, &other_code);
                if (protocol == other_protocol && code == other_code) {
                    duplicate = true;
                    break;
                }
            }
        }
        char value[56];
        if (protocol != 0 || code != 0) {
            snprintf(value, sizeof(value), "%s%u - 0x%016llX",
                     duplicate ? "Duplicate: protocol " : "Protocol ",
                     protocol, (unsigned long long)code);
        } else {
            strcpy(value, "Not learned");
        }
        char row[320];
        snprintf(row, sizeof(row),
            "<div class=irrow id=irr%u><div><b>%s</b><span id=irv%u%s>%s</span></div>"
            "<button onclick='learn(%u,\"%s\")'>Learn</button>"
            "<button class=secondary onclick='clearIr(%u,\"%s\")'>Clear</button></div>",
            i, irKeyName(i), i, duplicate ? " class=warn" : "", value,
            i, irKeyName(i), i, irKeyName(i));
        Esp8266BaseWeb::sendChunk(row);
    }
    Esp8266BaseWeb::sendContent_P(CONFIG_IR_END);

    Esp8266BaseWeb::sendFooter();
}

// API Handlers

void FanWeb::handleApiStatus() {
    if (!Esp8266BaseWeb::checkAuth()) return;
    
    char buf[1024];
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
        "{\"ok\":true,\"data\":{\"state\":\"%s\",\"speed\":%d,\"target_speed\":%d,\"gear\":%u,\"rpm\":%u,\"timer_remaining\":%lu,"
        "\"run_duration\":%lu,\"boot_run_duration\":%lu,\"blocked\":%s,\"min_speed\":%u,\"soft_start\":%u,\"soft_stop\":%u,"
        "\"block_detect\":%u,\"sleep_wait\":%u,\"auto_restore\":%s,\"led_flash_ms\":%u,"
        "\"ip\":\"%s\",\"rssi\":%ld,\"clock\":\"%s\","
        "\"ir_learning\":%s,\"ir_key\":%u,\"ir_remaining\":%lu,\"ir_learn_seq\":%lu,"
        "\"ir_reject_seq\":%lu,\"ir_duplicate_key\":%u,"
        "\"ir_last_protocol\":%u,\"ir_last_code\":\"0x%016llX\"}}",
        stateStr, _controller->getCurrentSpeed(), _controller->getTargetSpeed(),
        _controller->getCurrentGear(),
        _controller->getCurrentRpm(),
        (unsigned long)_controller->getTimerRemaining(),
        (unsigned long)_controller->getTotalRunDuration(),
        (unsigned long)_controller->getBootRunDuration(),
        _controller->isBlocked() ? "true" : "false",
        _controller->getMinEffectiveSpeed(),
        _controller->getSoftStartTime(),
        _controller->getSoftStopTime(),
        _controller->getBlockDetectTime(),
        _controller->getSleepWaitTime(),
        _controller->getAutoRestore() ? "true" : "false",
        _controller->getLedFlashDuration(),
        ip, rssi, clock,
        _ir->isLearning() ? "true" : "false", ir_key,
        (unsigned long)_ir->getLearningRemaining(),
        (unsigned long)_ir->getLearnedSequence(),
        (unsigned long)_ir->getLearnRejectSequence(),
        _ir->getDuplicateKeyIndex(),
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
        uint32_t speed = 0;
        if (parseUintArg(speedStr, 0, 100, &speed)) {
            ESP8266BASE_LOG_I("FanWeb", "user_action speed=%lu", static_cast<unsigned long>(speed));
            _controller->setSpeed(static_cast<uint8_t>(speed));
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%d,\"target_speed\":%d}",
                     _controller->getCurrentSpeed(), _controller->getTargetSpeed());
            server.send(200, "application/json", buf);
            return;
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
        uint32_t seconds = 0;
        if (parseUintArg(secStr, 0, 356400, &seconds)) {
            ESP8266BASE_LOG_I("FanWeb", "user_action timer_seconds=%lu", static_cast<unsigned long>(seconds));
            _controller->setTimer(seconds);
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"ok\":true,\"timer_remaining\":%lu}",
                     (unsigned long)_controller->getTimerRemaining());
            server.send(200, "application/json", buf);
            return;
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
        bool has_min_speed = false;
        bool has_soft_start = false;
        bool has_soft_stop = false;
        bool has_block_detect = false;
        bool has_sleep_wait = false;
        bool has_led_flash_ms = false;
        bool has_auto_restore = false;
        uint8_t min_speed = _controller->getMinEffectiveSpeed();
        uint16_t soft_start = _controller->getSoftStartTime();
        uint16_t soft_stop = _controller->getSoftStopTime();
        uint16_t block_detect = _controller->getBlockDetectTime();
        uint16_t sleep_wait = _controller->getSleepWaitTime();
        uint16_t led_flash_ms = _controller->getLedFlashDuration();
        bool auto_restore = _controller->getAutoRestore();

        if (server.hasArg("min_speed")) {
            uint32_t parsed = 0;
            if (!parseUintArg(server.arg("min_speed"), 0, 50, &parsed)) {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid min_speed\"}");
                return;
            }
            min_speed = static_cast<uint8_t>(parsed);
            has_min_speed = true;
        }
        if (server.hasArg("soft_start")) {
            uint32_t parsed = 0;
            if (!parseUintArg(server.arg("soft_start"), 0, 10000, &parsed)) {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid soft_start\"}");
                return;
            }
            soft_start = static_cast<uint16_t>(parsed);
            has_soft_start = true;
        }
        if (server.hasArg("soft_stop")) {
            uint32_t parsed = 0;
            if (!parseUintArg(server.arg("soft_stop"), 0, 10000, &parsed)) {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid soft_stop\"}");
                return;
            }
            soft_stop = static_cast<uint16_t>(parsed);
            has_soft_stop = true;
        }
        if (server.hasArg("block_detect")) {
            uint32_t parsed = 0;
            if (!parseUintArg(server.arg("block_detect"), 100, 5000, &parsed)) {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid block_detect\"}");
                return;
            }
            block_detect = static_cast<uint16_t>(parsed);
            has_block_detect = true;
        }
        if (server.hasArg("sleep_wait")) {
            uint32_t parsed = 0;
            if (!parseUintArg(server.arg("sleep_wait"), 0, 3600, &parsed)) {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid sleep_wait\"}");
                return;
            }
            sleep_wait = static_cast<uint16_t>(parsed);
            has_sleep_wait = true;
        }
        if (server.hasArg("led_flash_ms")) {
            uint32_t parsed = 0;
            if (!parseUintArg(server.arg("led_flash_ms"), 0, 2000, &parsed)) {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid led_flash_ms\"}");
                return;
            }
            led_flash_ms = static_cast<uint16_t>(parsed);
            has_led_flash_ms = true;
        }
        if (server.hasArg("auto_restore")) {
            uint32_t parsed = 0;
            if (!parseUintArg(server.arg("auto_restore"), 0, 1, &parsed)) {
                server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid auto_restore\"}");
                return;
            }
            auto_restore = parsed != 0;
            has_auto_restore = true;
        }

        if (has_min_speed && min_speed != _controller->getMinEffectiveSpeed()) {
            _controller->setMinEffectiveSpeed(min_speed);
            changed++;
        }
        if (has_soft_start && soft_start != _controller->getSoftStartTime()) {
            _controller->setSoftStartTime(soft_start);
            changed++;
        }
        if (has_soft_stop && soft_stop != _controller->getSoftStopTime()) {
            _controller->setSoftStopTime(soft_stop);
            changed++;
        }
        if (has_block_detect && block_detect != _controller->getBlockDetectTime()) {
            _controller->setBlockDetectTime(block_detect);
            changed++;
        }
        if (has_sleep_wait && sleep_wait != _controller->getSleepWaitTime()) {
            _controller->setSleepWaitTime(sleep_wait);
            changed++;
        }
        if (has_led_flash_ms && led_flash_ms != _controller->getLedFlashDuration()) {
            _controller->setLedFlashDuration(led_flash_ms);
            changed++;
        }
        if (has_auto_restore && auto_restore != _controller->getAutoRestore()) {
            _controller->setAutoRestore(auto_restore);
            changed++;
        }

        bool flushed = Esp8266BaseConfig::flush();
        if (flushed && changed > 0) {
            _controller->notifyUserAction();
        }
        ESP8266BASE_LOG_I("FanWeb", "config_save_complete changed=%u flushed=%u",
                          (unsigned)changed, flushed ? 1U : 0U);
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"ok\":%s,\"changed\":%u,\"flushed\":%s,\"data\":{\"min_effective_speed\":%d,\"soft_start\":%d,\"soft_stop\":%d,"
            "\"block_detect\":%d,\"sleep_wait\":%d,\"led_flash_ms\":%d,\"auto_restore\":%s}}",
            flushed ? "true" : "false",
            (unsigned)changed,
            flushed ? "true" : "false",
            _controller->getMinEffectiveSpeed(),
            _controller->getSoftStartTime(),
            _controller->getSoftStopTime(),
            _controller->getBlockDetectTime(),
            _controller->getSleepWaitTime(),
            _controller->getLedFlashDuration(),
            _controller->getAutoRestore() ? "true" : "false"
        );
        server.send(flushed ? 200 : 500, "application/json", buf);
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"data\":{\"min_effective_speed\":%d,\"soft_start\":%d,\"soft_stop\":%d,"
            "\"block_detect\":%d,\"sleep_wait\":%d,\"led_flash_ms\":%d,\"auto_restore\":%s}}",
            _controller->getMinEffectiveSpeed(),
            _controller->getSoftStartTime(),
            _controller->getSoftStopTime(),
            _controller->getBlockDetectTime(),
            _controller->getSleepWaitTime(),
            _controller->getLedFlashDuration(),
            _controller->getAutoRestore() ? "true" : "false"
        );
        server.send(200, "application/json", buf);
    }
}

void FanWeb::handleApiIrLearn() {
    if (!Esp8266BaseWeb::checkAuth()) return;

    auto& server = Esp8266BaseWeb::server();
    if (server.method() == HTTP_POST && server.hasArg("key_index")) {
        uint32_t idx = 0;
        if (parseUintArg(server.arg("key_index"), 0, IR_KEY_COUNT - 1, &idx)) {
            if (server.hasArg("clear")) {
                bool changed = _controller->clearIRCode(static_cast<uint8_t>(idx));
                if (changed) _controller->notifyUserAction();
                ESP8266BASE_LOG_I("FanWeb", "user_action ir_clear key=%lu changed=%u",
                                  static_cast<unsigned long>(idx), changed ? 1U : 0U);
                char buf[64];
                snprintf(buf, sizeof(buf), "{\"ok\":true,\"changed\":%s}", changed ? "true" : "false");
                server.send(200, "application/json", buf);
                return;
            }
            if (_ir->startLearning(static_cast<uint8_t>(idx))) {
                ESP8266BASE_LOG_I("FanWeb", "user_action ir_learn key=%lu", static_cast<unsigned long>(idx));
                char buf[128];
                snprintf(buf, sizeof(buf), "{\"ok\":true,\"learning\":true,\"timeout\":10,\"seq\":%lu,\"rej_seq\":%lu}",
                         (unsigned long)_ir->getLearnedSequence(),
                         (unsigned long)_ir->getLearnRejectSequence());
                server.send(200, "application/json", buf);
                return;
            }
        }
    }
    ESP8266BASE_LOG_W("FanWeb", "invalid_ir_learn_request");
    server.send(400, "application/json", "{\"error\":\"invalid request\"}");
}
