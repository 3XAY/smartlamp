#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h> 
#include <rom/rtc.h> 
#include <ArduinoOTA.h>  
#include "time.h"
#include "secrets.h" 

#define IR_RECEIVE_PIN 15  
#include <IRremote.hpp>

// --- NeoPixel Library & Hardware Configuration ---
#include <Adafruit_NeoPixel.h>
#define NEOPIXEL_PIN    4
#define NEOPIXEL_COUNT  27  
Adafruit_NeoPixel strip(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// --- Hardware Pin Definitions ---
#define BUTTON_PIN      13    
#define AUDIO_PIN       19    
#define POWER_SENSE_PIN 34    

// --- The Complete 21 NEC Hex Command Matrix ---
#define CMD_POWER    0x00
#define CMD_UP       0x01
#define CMD_LIGHT    0x02
#define CMD_LEFT     0x04
#define CMD_SOUND    0x05
#define CMD_RIGHT    0x06
#define CMD_BACK     0x08
#define CMD_DOWN     0x09
#define CMD_FORWARD  0x0A
#define CMD_PLUS     0x0C
#define CMD_ZERO     0x0D
#define CMD_MINUS    0x0E
#define CMD_ONE      0x10
#define CMD_TWO      0x11
#define CMD_THREE    0x12
#define CMD_FOUR     0x14
#define CMD_FIVE     0x15
#define CMD_SIX      0x16
#define CMD_SEVEN    0x18
#define CMD_EIGHT    0x19
#define CMD_NINE     0x1A

// --- CIE1931 Human Eye Perceptual Brightness Curve (0-255 Scale Map) ---
const uint8_t CIE1931_CURVE[101] PROGMEM = {
    0,   0,   0,   0,   0,   1,   1,   1,   1,   2,   2,   2,   3,   3,   4,   4,
    5,   5,   6,   7,   7,   8,   9,  10,  11,  12,  13,  14,  15,  16,  18,  19,
    20,  22,  24,  25,  27,  29,  31,  33,  35,  37,  40,  42,  44,  47,  49,  52,
    55,  58,  61,  64,  67,  71,  74,  78,  82,  85,  89,  94,  98, 102, 107, 111,
    116, 121, 126, 131, 136, 142, 147, 153, 159, 165, 171, 177, 184, 190, 197, 204,
    211, 218, 225, 233, 240, 248, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255
};

struct ColorTemp {
    const char* name;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t default_bright;
};

const ColorTemp COLOR_TEMPS[5] = {
    {"COOL (6500K)",   255, 245, 255, 100},
    {"DAYLIGHT (5000K)",     255, 238, 215, 100},
    {"NEUTRAL (4000K)",255, 214, 170, 100},
    {"WARM (3000K)",   255, 180, 107, 100},
    {"ULTRA WARM (2200K)",   255, 140, 40,  100}
};
#define TOTAL_TEMPS 5

volatile bool led_is_on = false;
volatile uint8_t brightness_pct = 50;
volatile uint8_t current_temp_index = 2; 
const uint8_t BRIGHTNESS_STEP_PCT = 5; 

volatile uint8_t volume_mode = 1; 
uint8_t last_valid_ir_cmd = 0xFF;

uint8_t profile_r[3] = {255, 0, 0};
uint8_t profile_g[3] = {0, 255, 0};
uint8_t profile_b[3] = {0, 0, 255};
uint8_t profile_bright[3] = {50, 50, 50};
volatile bool custom_profile_active = false;
volatile bool rainbow_mode_active = false;
volatile uint8_t selected_profile_idx = 0;

// Render Transition Smoothing Engine Trackers
float current_render_brightness = 0.0f;
float current_render_r[NEOPIXEL_COUNT] = {0};
float current_render_g[NEOPIXEL_COUNT] = {0};
float current_render_b[NEOPIXEL_COUNT] = {0};

const float FADE_SPEED_FACTOR = 0.10f; 
unsigned long last_fade_time = 0;
const uint8_t FADE_INTERVAL = 16; 

volatile bool strip_needs_update = false;

struct FlashCache {
    bool led_on;
    uint8_t bright_pct;
    uint8_t temp_idx;
    uint8_t vol_mode;
    bool custom_active;
    bool rainbow_active;
    uint8_t custom_idx;
} storage_cache;

struct RamBackup {
    bool led_on;
    uint8_t bright_pct;
    uint8_t temp_idx;
    bool custom_active;
    bool rainbow_active;
    uint8_t custom_idx;
} ram_backup_cache;

bool monitor_armed = false; 
bool wifi_init_complete = false;
int power_drop_threshold = 0; 
uint8_t consecutive_drop_counter = 0;
const uint8_t TRIGGER_CONSECUTIVE_LIMIT = 15; 

volatile bool ota_enabled = false; 
bool ota_initialized = false;
bool timer_active = false;
unsigned long timer_start_ms = 0;
volatile unsigned long timer_duration_ms = 1500000; 

bool sleep_mode_active = false;
unsigned long sleep_start_ms = 0;
const unsigned long SLEEP_DURATION_MS = 7200000; 

WebServer server(80);
Preferences preferences; 
const char* ntpServer = "pool.ntp.org";
const char* tzEST = "EST5EDT,M3.2.0,M11.1.0"; 

// --- High-Contrast, Bold Responsive WebUI Dashboard ---
const char HTML_TEMPLATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no'><title>LAMP</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;font-weight:800;background:#000000;color:#ffffff;margin:0;padding:16px;display:flex;justify-content:center;letter-spacing:0.5px;-webkit-tap-highlight-color:transparent;}
.container{width:100%;max-width:440px;display:flex;flex-direction:column;gap:16px;box-sizing:border-box;}
header{display:flex;justify-content:between;align-items:center;padding:8px 4px;width:100%;box-sizing:border-box;}
.clock-section{display:flex;flex-direction:column;gap:2px;}
#clock{font-size:36px;font-weight:900;color:#FFAA00;letter-spacing:-1.5px;line-height:1;}
#countdown{font-size:12px;font-weight:900;color:#FFAA00;letter-spacing:1px;min-height:14px;}
.status-area{display:flex;align-items:center;gap:8px;background:#111;padding:6px 12px;border-radius:20px;border:1px solid #222;margin-left:auto;}
.status-dot{width:8px;height:8px;border-radius:50%;background:#333;transition:background 0.15s;}
.status-active{background:#FFAA00;box-shadow:0 0 10px #FFAA00;}
#state-txt{font-size:11px;font-weight:900;letter-spacing:1.5px;text-transform:uppercase;color:#888;}
.btn{background:#111;color:#fff;border:2px solid #222;border-radius:12px;padding:14px;cursor:pointer;font-weight:900;font-size:13px;text-transform:uppercase;letter-spacing:1px;display:flex;align-items:center;justify-content:center;gap:10px;transition:all 0.12s;box-sizing:border-box;min-height:48px;}
.btn:hover, .btn:active{background:#161616;border-color:#333;}
.btn-primary{background:#FFAA00;color:#000;border:none;font-weight:900;font-size:15px;min-height:52px;}
.btn-primary:hover, .btn-primary:active{background:#ffbb33;}
.btn-active{border-color:#FFAA00;color:#FFAA00;background:#0d0900;}
.grid-2{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
.grid-3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;}
.grid-4{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;}
.card{background:#0a0a0a;border:2px solid #161616;border-radius:16px;padding:20px;display:flex;flex-direction:column;gap:16px;box-sizing:border-box;}
.card-header{font-size:11px;font-weight:900;text-transform:uppercase;letter-spacing:2px;color:#555;margin-bottom:2px;}
.slider-wrapper{display:flex;flex-direction:column;gap:8px;}
.slider-header{display:flex;justify-content:between;font-size:12px;font-weight:900;text-transform:uppercase;letter-spacing:1px;color:#777;}
.slider{-webkit-appearance:none;width:100%;height:8px;border-radius:4px;background:#1a1a1a;outline:none;margin:12px 0;box-sizing:border-box;}
.slider::-webkit-slider-thumb{-webkit-appearance:none;width:24px;height:24px;border-radius:50%;background:#FFAA00;cursor:pointer;}
.color-container{position:relative;width:100%;height:48px;border-radius:12px;overflow:hidden;border:2px solid #222;box-sizing:border-box;}
.picker-input{position:absolute;top:-10px;left:-10px;width:calc(100% + 20px);height:calc(100% + 20px);border:none;background:none;cursor:pointer;}
.num-input{background:#111;color:#fff;border:2px solid #222;padding:10px;border-radius:12px;font-size:14px;font-weight:900;text-align:center;outline:none;width:80px;height:44px;box-sizing:border-box;-moz-appearance:textfield;}
.num-input::-webkit-outer-spin-button,.num-input::-webkit-inner-spin-button{-webkit-appearance:none;margin:0;}
.num-input:focus{border-color:#FFAA00;}
.toggle-row{display:flex;justify-content:between;align-items:center;font-size:12px;font-weight:900;text-transform:uppercase;letter-spacing:1px;color:#777;}
.switch{position:relative;display:inline-block;width:44px;height:26px;}
.switch input{opacity:0;width:0;height:0;}
.switch-slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#1a1a1a;border:2px solid #333;border-radius:26px;transition:.15s;}
.switch-slider:before{position:absolute;content:"";height:16px;width:16px;left:3px;bottom:3px;background-color:#555;border-radius:50%;transition:.15s;}
input:checked + .switch-slider{background-color:#FFAA00;border-color:#FFAA00;}
input:checked + .switch-slider:before{transform:translateX(18px);background-color:#000;}
.btn-save-large{padding:16px;background:#111;border-color:#222;min-height:50px;}
.btn-save-large svg{width:22px;height:22px;color:#FFAA00;}
svg{width:20px;height:20px;fill:currentColor;flex-shrink:0;}
@media(min-width:480px){
    body{padding:40px 24px;}
    .container{gap:20px;}
    .card{padding:24px;gap:18px;}
    #clock{font-size:42px;}
}
</style></head>
<body><div class='container'>
<header>
<div class='clock-section'><div id='clock'>--:-- --</div><div id='countdown'></div></div>
<div class='status-area'><span id='status-dot' class='status-dot'></span><span id='state-txt'>--</span></div>
</header>

<button class='btn btn-primary' onclick='runCtrl("toggle=1")'><svg viewBox='0 0 24 24'><path d='M12,3A9,9 0 0,0 3,12A9,9 0 0,0 12,21A9,9 0 0,0 21,12A9,9 0 0,0 12,3M12,5A7,7 0 0,1 19,12A7,7 0 0,1 12,19A7,7 0 0,1 5,12A7,7 0 0,1 12,5M12,7A5,5 0 0,0 7,12A5,5 0 0,0 12,17A5,5 0 0,0 17,12A5,5 0 0,0 12,7Z'/></svg>POWER</button>

<div class='card'>
<div class='card-header'>LIGHTING</div>
<div class='slider-wrapper'><div style='display:flex;justify-content:space-between;' class='slider-header'><span>INTENSITY</span><span id='b-pct'>0%</span></div>
<input type='range' min='0' max='100' step='5' class='slider' id='bSlider' oninput='sliderDrag(this.value)' onchange='sliderRelease(this.value)'></div>
<div class='grid-2'><button class='btn' onclick='runCtrl("nav=left")'><svg viewBox='0 0 24 24'><path d='M15.41,16.58L10.83,12L15.41,7.41L14,6L8,12L14,18L15.41,16.58Z'/></svg>COOL</button>
<button class='btn' onclick='runCtrl("nav=right")'>WARM<svg viewBox='0 0 24 24'><path d='M8.59,16.58L13.17,12L8.59,7.41L10,6L16,12L10,18L8.59,16.58Z'/></svg></button></div>
<div style='font-size:12px;color:#aaa;text-align:center;letter-spacing:1px;' id='temp-txt'>--</div></div>

<div class='card'>
<div class='card-header'>PRESETS & EFFECTS</div>
<div class='grid-4'>
<button class='btn' id='pBtn0' onclick='changeSelectedProfile(0)'><svg viewBox='0 0 24 24'><path d='M12,17.27L18.18,21L16.54,13.97L22,9.24L14.81,8.62L12,2L9.19,8.62L2,9.24L7.45,13.97L5.82,21L12,17.27Z'/></svg></button>
<button class='btn' id='pBtn1' onclick='changeSelectedProfile(1)'><svg viewBox='0 0 24 24'><path d='M12,21.35L10.55,20.03C5.4,15.36 2,12.27 2,8.5C2,5.41 4.42,3 7.5,3C9.24,3 10.91,3.81 12,5.08C13.09,3.81 14.76,3 16.5,3C19.58,3 22,5.41 22,8.5C22,12.27 18.6,15.36 13.45,20.03L12,21.35Z'/></svg></button>
<button class='btn' id='pBtn2' onclick='changeSelectedProfile(2)'><svg viewBox='0 0 24 24'><path d='M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4A8,8 0 0,1 20,12A8,8 0 0,1 12,20A8,8 0 0,1 4,12A8,8 0 0,1 12,4Z'/></svg></button>
<button class='btn' id='rbBowBtn' onclick='runCtrl("rainbow=toggle")' style='background:linear-gradient(45deg,#ff0000,#00ff00,#0000ff); border:none;'></button></div>
<div class='color-container' id='pickerWrapper'><input type='color' id='cPicker' class='picker-input' oninput='previewColor(this.value)'></div>
<button class='btn btn-save-large' onclick='saveActiveProfile()'><svg viewBox='0 0 24 24'><path d='M15,9H5V5H15M12,19A3,3 0 0,1 9,16A3,3 0 0,1 12,13A3,3 0 0,1 15,16A3,3 0 0,1 12,19M17,3H5C3.89,3 3,3.9 3,5V19A2,2 0 0,0 5,21H19A2,2 0 0,0 21,19V7L17,3Z'/></svg></button></div>

<div class='card'>
<div class='card-header'>AUTOMATION</div>
<div style='display:flex;gap:10px;align-items:center;justify-content:space-between;'><span style='font-size:12px;color:#777;letter-spacing:1px;'>TIMER</span>
<div style='display:flex;gap:8px;align-items:center;'><input type='number' id='tmMinutes' class='num-input' min='1' max='1440' placeholder='MINUTES' value='25'><button class='btn' id='timerBtn' style='padding:0 16px; height:44px;' onclick='sendValidatedTimer()'><svg viewBox='0 0 24 24'><path d='M12,20A8,8 0 0,0 20,12A8,8 0 0,0 12,4A8,8 0 0,0 4,12A8,8 0 0,0 12,20M12,2A10,10 0 0,1 22,12A10,10 0 0,1 12,22A10,10 0 0,1 2,12A10,10 0 0,1 12,4M12.5,7V12.25L17,14.92L16.25,16.15L11,13V7H12.5Z'/></svg></button></div></div>
<button class='btn' id='sleepBtn' onclick='runCtrl("sleep=1")'><svg viewBox='0 0 24 24'><path d='M17.75,21.16C16.5,21.16 15.2,20.81 14.04,20.09C11,18.19 9.93,14.22 11.83,11.18C12.63,9.89 13.84,8.96 15.25,8.5C14.7,7.96 14,7.56 13.19,7.36C10.15,6.6 7.11,8.47 6.36,11.5C5.6,14.55 7.47,17.59 10.5,18.35C11.56,18.61 12.64,18.5 13.6,18.06C12.83,19.43 12.64,21 13.11,22.43C11.16,22.94 9.08,22.71 7.27,21.75C3.36,19.3 2.14,14.19 4.58,10.28C7,6.36 12.14,5.14 16.05,7.58C18.4,9.05 19.81,11.5 20,14.12C21.45,14.19 22.84,14.86 23.83,16.03C21.43,16.03 19.34,18.13 19.34,20.53C19.34,20.75 19.36,20.96 19.4,21.16C18.85,21.16 18.3,21.16 17.75,21.16Z'/></svg>SLEEP</button>
<div style='display:flex;flex-direction:column;gap:10px;margin-top:4px;'>
<div class='slider-header'><span>VOLUME</span></div>
<div class='grid-3'>
<button class='btn' id='vMode0' onclick='runCtrl("volume=0")'>MUTE</button>
<button class='btn' id='vMode1' onclick='runCtrl("volume=1")'>MID</button>
<button class='btn' id='vMode2' onclick='runCtrl("volume=2")'>MAX</button></div></div>
<div style='display:flex; justify-content:space-between; align-items:center; border-top:2px solid #161616; padding-top:16px; margin-top:4px;' class='toggle-row'><span>OTA FLASH</span><label class='switch'><input type='checkbox' id='otaChk' onchange='runCtrl("otaon="+(this.checked?1:0))'><span class='switch-slider'></span></label></div></div>
</div>
<script>
let localTime = new Date();
let userInteracting = false; let interactionTimeout = null;
let activeProfIdx = 0;
let profilesData = [[255,0,0], [0,255,0], [0,0,255]];

function updateClockView(){
    localTime.setSeconds(localTime.getSeconds() + 1);
    let h = localTime.getHours(); let m = localTime.getMinutes();
    let ampm = h >= 12 ? 'PM' : 'AM'; h = h % 12; h = h ? h : 12; m = m < 10 ? '0' + m : m;
    document.getElementById('clock').innerText = h + ':' + m + ' ' + ampm;
}
setInterval(updateClockView, 1000);

function sendValidatedTimer() {
    let inputEl = document.getElementById('tmMinutes'); let rawVal = parseFloat(inputEl.value);
    if (isNaN(rawVal) || !Number.isInteger(rawVal) || rawVal < 1 || rawVal > 1440) { alert("Enter an integer (1-1440)."); return; }
    runCtrl("timer=toggle&dur_mins=" + rawVal);
}
function sliderDrag(v){
    document.getElementById('b-pct').innerText = v + '%'; userInteracting = true; clearTimeout(interactionTimeout);
    fetch('/api/control?brightness=' + v + '&nocache=' + Date.now()).then(r=>r.json()).then(renderData).catch(e=>0);
}
function sliderRelease(v){ interactionTimeout = setTimeout(() => { userInteracting = false; }, 1500); }
function rgbToHex(r, g, b) { return "#" + ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1); }
function hexToRgb(hex) { let result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex); return result ? { r: parseInt(result[1], 16), g: parseInt(result[2], 16), b: parseInt(result[3], 16) } : null; }

function changeSelectedProfile(idx) {
    activeProfIdx = idx; runCtrl("profl0ad=" + idx);
}
function previewColor(hexVal) {
    let rgb = hexToRgb(hexVal);
    let currentBright = document.getElementById('bSlider').value;
    if(rgb) {
        profilesData[activeProfIdx] = [rgb.r, rgb.g, rgb.b];
        document.getElementById('pickerWrapper').style.backgroundColor = hexVal;
        fetch('/api/control?profprev=1&idx='+activeProfIdx+'&r='+rgb.r+'&g='+rgb.g+'&b='+rgb.b+'&b_val='+currentBright+'&nocache='+Date.now()).then(r=>r.json()).then(renderData).catch(e=>0);
    }
}
function saveActiveProfile() { 
    let currentBright = document.getElementById('bSlider').value;
    runCtrl("profsave=" + activeProfIdx + "&b_val=" + currentBright); 
}

function renderData(d){
    let dot = document.getElementById('status-dot');
    let txt = document.getElementById('state-txt');
    if(d.led_is_on) { dot.className='status-dot status-active'; txt.innerText = d.sleep_active ? "SLEEP" : "ON"; }
    else { dot.className='status-dot'; txt.innerText = "OFF"; }
    
    document.getElementById('temp-txt').innerText = d.current_temp_name;
    document.getElementById('otaChk').checked = d.ota_link;
    
    let tBtn = document.getElementById('timerBtn');
    let countdownEl = document.getElementById('countdown');
    if(d.timer_active) { 
        tBtn.classList.add('btn-active');
        countdownEl.innerText = d.timer_left_mins + "M LEFT";
    } else { 
        tBtn.classList.remove('btn-active'); 
        countdownEl.innerText = "";
    }
    
    let sBtn = document.getElementById('sleepBtn');
    if(d.sleep_active) { sBtn.classList.add('btn-active'); } else { sBtn.classList.remove('btn-active'); }
    
    let rbBtn = document.getElementById('rbBowBtn');
    if(d.rainbow_active) { rbBtn.style.border = "2px solid #FFAA00"; } else { rbBtn.style.border = "none"; }

    if(!userInteracting) { document.getElementById('bSlider').value=d.brightness_pct; document.getElementById('b-pct').innerText = d.brightness_pct + '%'; }
    for(let i=0;i<3;i++){ let btn = document.getElementById('vMode'+i); if(btn) btn.className = (d.volume_mode===i) ? "btn btn-active" : "btn"; }
    if(d.profs) {
        profilesData = d.profs; activeProfIdx = d.cur_prof;
        for(let i=0; i<3; i++) { document.getElementById('pBtn'+i).className = (i===activeProfIdx) ? "btn btn-active" : "btn"; }
        let rgb = profilesData[activeProfIdx]; 
        let currentHex = rgbToHex(rgb[0], rgb[1], rgb[2]);
        document.getElementById('cPicker').value = currentHex;
        document.getElementById('pickerWrapper').style.backgroundColor = currentHex;
    }
    if(d.time){ let parts = d.time.split(':'); if(parts.length === 3){ localTime.setHours(parseInt(parts[0]), parseInt(parts[1]), parseInt(parts[2])); updateClockView(); } }
}
function getStat(){ if(userInteracting) return; fetch('/api/status?nocache=' + Date.now()).then(r=>r.json()).then(renderData).catch(e=>0); }
function runCtrl(p){ fetch('/api/control?'+p+'&nocache=' + Date.now()).then(r=>r.json()).then(renderData).catch(e=>0); }
setInterval(getStat, 350); getStat();
</script></body></html>
)rawliteral";

TaskHandle_t NetworkTaskHandle;
void handleStatus();
void handleControl();
void update_led_strip_buffer();
void broadcastIPAudio();
void verifyAndSaveStateToFlash();
void backupToRam();
void restoreFromRam();

void triggerBeep(int duration_ms, int frequency = 230, bool max_volume_override = false) {
    if (volume_mode == 0 && !max_volume_override) return; 
    uint8_t dutyValue = (max_volume_override || volume_mode == 2) ? 28 : 3; 
    analogWriteFrequency(AUDIO_PIN, frequency); 
    analogWrite(AUDIO_PIN, dutyValue);            
    delay(duration_ms);
    analogWrite(AUDIO_PIN, 0); 
}

void NetworkWorkerLoop(void * pvParameters) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(50));

    bool connected = false;
    while (!connected) {
        int found_networks = WiFi.scanNetworks();
        if (found_networks == WIFI_SCAN_FAILED) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        for (int i = WIFI_NETWORK_COUNT - 1; i >= 0; i--) {
            const char* current_ssid = WIFI_NETWORKS[i].ssid;
            const char* current_pass = WIFI_NETWORKS[i].password;

            bool network_visible = false;
            for (int j = 0; j < found_networks; j++) {
                if (WiFi.SSID(j) == String(current_ssid)) {
                    network_visible = true;
                    break;
                }
            }
            if (!network_visible) continue;

            for (uint8_t attempt = 1; attempt <= 2; attempt++) {
                if (current_pass && strlen(current_pass) > 0) {
                    WiFi.begin(current_ssid, current_pass);
                } else {
                    WiFi.begin(current_ssid);
                }

                unsigned long tracking_start_ms = millis();
                while (millis() - tracking_start_ms < 10000) {
                    if (WiFi.status() == WL_CONNECTED) {
                        connected = true;
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
                if (connected) break;
                WiFi.disconnect();
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            if (connected) break;
        }
        if (!connected) vTaskDelay(pdMS_TO_TICKS(5000));
    }

    WiFi.scanDelete(); 
    configTzTime(tzEST, ntpServer);

    server.on("/", []() {
        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.send_P(200, "text/html", HTML_TEMPLATE);
    });
    server.on("/api/status", handleStatus);
    server.on("/api/control", handleControl);
    server.begin();

    wifi_init_complete = true;

    while (true) {
        server.handleClient();
        if (ota_enabled) {
            if (!ota_initialized) {
                ArduinoOTA.setHostname("Smart-Desktop-Lamp");
                ArduinoOTA.begin();
                ota_initialized = true;
            }
            ArduinoOTA.handle();
        }
        vTaskDelay(pdMS_TO_TICKS(2)); 
    }
}

void handleStatus() {
    struct tm timeinfo;
    char time_str[9] = "00:00:00";
    if (getLocalTime(&timeinfo)) {
        strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
    }
    
    unsigned long left_mins = 0;
    if (timer_active) {
        unsigned long now = millis();
        if (now - timer_start_ms < timer_duration_ms) {
            left_mins = ((timer_duration_ms - (now - timer_start_ms)) / 60000) + 1;
        }
    }

    String json;
    json.reserve(512); 
    
    String temp_name = "COOL TO WARM";
    if (rainbow_mode_active) temp_name = "RAINBOW MODE";
    else if (custom_profile_active) temp_name = "SLOT " + String(selected_profile_idx + 1);
    else temp_name = String(COLOR_TEMPS[current_temp_index].name);

    json = "{\"led_is_on\":" + String(led_is_on ? "true" : "false") + 
           ",\"brightness_pct\":" + String(brightness_pct) + 
           ",\"current_temp_name\":\"" + temp_name + "\"" + 
           ",\"volume_mode\":" + String(volume_mode) +
           ",\"ota_link\":" + String(ota_enabled ? "true" : "false") +
           ",\"timer_active\":" + String(timer_active ? "true" : "false") +
           ",\"timer_left_mins\":" + String(left_mins) +
           ",\"sleep_active\":" + String(sleep_mode_active ? "true" : "false") +
           ",\"rainbow_active\":" + String(rainbow_mode_active ? "true" : "false") +
           ",\"cur_prof\":" + String(selected_profile_idx) +
           ",\"profs\":[[" + String(profile_r[0]) + "," + String(profile_g[0]) + "," + String(profile_b[0]) + "],"
                       "[" + String(profile_r[1]) + "," + String(profile_g[1]) + "," + String(profile_b[1]) + "],"
                       "[" + String(profile_r[2]) + "," + String(profile_g[2]) + "," + String(profile_b[2]) + "]],"
           "\"time\":\"" + String(time_str) + "\"}";
    server.send(200, "application/json", json);
}

void handleControl() {
    if (server.hasArg("toggle")) { 
        led_is_on = !led_is_on;
        if(!led_is_on) {
            timer_active = false;
            sleep_mode_active = false;
        }
        strip_needs_update = true;
        triggerBeep(15, 260); 
    }
    if (server.hasArg("brightness")) {
        brightness_pct = constrain(server.arg("brightness").toInt(), 0, 100);
        led_is_on = (brightness_pct > 0); 
        if (custom_profile_active) {
            profile_bright[selected_profile_idx] = brightness_pct;
        }
        strip_needs_update = true;
    }
    if (server.hasArg("nav")) {
        custom_profile_active = false; 
        rainbow_mode_active = false;
        if (server.arg("nav") == "left") { 
            if (current_temp_index > 0) current_temp_index--;
        } else { 
            if (current_temp_index < TOTAL_TEMPS - 1) current_temp_index++;
        }
        brightness_pct = COLOR_TEMPS[current_temp_index].default_bright;
        strip_needs_update = true;
        triggerBeep(15, 260); 
    }
    if (server.hasArg("volume")) {
        volume_mode = constrain(server.arg("volume").toInt(), 0, 2);
        triggerBeep(15, 260); 
    }
    if (server.hasArg("otaon")) {
        ota_enabled = (server.arg("otaon").toInt() == 1);
    }
    if (server.hasArg("rainbow")) {
        rainbow_mode_active = !rainbow_mode_active;
        custom_profile_active = false;
        if (rainbow_mode_active) {
            led_is_on = true;
            brightness_pct = 50; 
        }
        strip_needs_update = true;
        triggerBeep(15, 260);
    }
    if (server.hasArg("profprev")) {
        uint8_t idx = constrain(server.arg("idx").toInt(), 0, 2);
        profile_r[idx] = constrain(server.arg("r").toInt(), 0, 255);
        profile_g[idx] = constrain(server.arg("g").toInt(), 0, 255);
        profile_b[idx] = constrain(server.arg("b").toInt(), 0, 255);
        if (server.hasArg("b_val")) {
            profile_bright[idx] = constrain(server.arg("b_val").toInt(), 0, 100);
        }
        selected_profile_idx = idx;
        custom_profile_active = true;
        rainbow_mode_active = false;
        led_is_on = true;
        brightness_pct = profile_bright[idx];
        strip_needs_update = true;
    }
    if (server.hasArg("profl0ad")) {
        selected_profile_idx = constrain(server.arg("profl0ad").toInt(), 0, 2);
        custom_profile_active = true;
        rainbow_mode_active = false;
        led_is_on = true;
        brightness_pct = profile_bright[selected_profile_idx];
        strip_needs_update = true;
        triggerBeep(15, 260); 
    }
    if (server.hasArg("profsave")) {
        uint8_t idx = constrain(server.arg("profsave").toInt(), 0, 2);
        if (server.hasArg("b_val")) {
            profile_bright[idx] = constrain(server.arg("b_val").toInt(), 0, 100);
        }
        preferences.begin("lamp-state", false);
        String keyR = "p" + String(idx) + "_r";
        String keyG = "p" + String(idx) + "_g";
        String keyB = "p" + String(idx) + "_b";
        String keyBR = "p" + String(idx) + "_br";
        preferences.putInt(keyR.c_str(), profile_r[idx]);
        preferences.putInt(keyG.c_str(), profile_g[idx]);
        preferences.putInt(keyB.c_str(), profile_b[idx]);
        preferences.putInt(keyBR.c_str(), profile_bright[idx]);
        preferences.end();
        triggerBeep(80, 440); 
    }

    if (server.hasArg("timer")) {
        if (!timer_active) { 
            backupToRam();
            if (server.hasArg("dur_mins")) {
                int mins = constrain(server.arg("dur_mins").toInt(), 1, 1440);
                timer_duration_ms = (unsigned long)mins * 60000;
            } else {
                timer_duration_ms = 1500000;
            }
            timer_start_ms = millis(); 
            led_is_on = true;
            if (brightness_pct == 0) brightness_pct = 50;
            timer_active = true;
        } else {
            timer_active = false;
            restoreFromRam(); 
        }
        strip_needs_update = true;
        triggerBeep(15, 260); 
    }
    if (server.hasArg("sleep")) {
        if (sleep_mode_active) {
            sleep_mode_active = false;
            restoreFromRam();
        } else {
            backupToRam();
            led_is_on = true;
            brightness_pct = 25; 
            sleep_mode_active = true;
            sleep_start_ms = millis();
        }
        strip_needs_update = true;
        triggerBeep(15, 260); 
    }
    handleStatus();
}

void setup() {
    Serial.begin(115200);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(AUDIO_PIN, OUTPUT);
    analogWrite(AUDIO_PIN, 0);
    pinMode(POWER_SENSE_PIN, INPUT);
    analogWriteResolution(AUDIO_PIN, 8); 

    strip.begin();
    strip.setBrightness(255);
    strip.show(); 
    
    gpio_set_drive_capability((gpio_num_t)NEOPIXEL_PIN, GPIO_DRIVE_CAP_3); 

    preferences.begin("lamp-state", false);
    storage_cache.led_on = led_is_on = preferences.getBool("led_on", false);
    storage_cache.bright_pct = brightness_pct = preferences.getInt("bright_pct", 50);
    storage_cache.temp_idx = current_temp_index = preferences.getInt("temp_idx", 2);
    storage_cache.vol_mode = volume_mode = preferences.getInt("vol_mode", 1);
    storage_cache.custom_active = custom_profile_active = preferences.getBool("cust_act", false);
    storage_cache.rainbow_active = rainbow_mode_active = preferences.getBool("rbow_act", false);
    storage_cache.custom_idx = selected_profile_idx = preferences.getInt("cust_idx", 0);
    
    profile_r[0] = preferences.getInt("p0_r", 255);
    profile_g[0] = preferences.getInt("p0_g", 0);
    profile_b[0] = preferences.getInt("p0_b", 0);
    profile_bright[0] = preferences.getInt("p0_br", 50);
    
    profile_r[1] = preferences.getInt("p1_r", 0);
    profile_g[1] = preferences.getInt("p1_g", 255);
    profile_b[1] = preferences.getInt("p1_b", 0);
    profile_bright[1] = preferences.getInt("p1_br", 50);
    
    profile_r[2] = preferences.getInt("p2_r", 0);
    profile_g[2] = preferences.getInt("p2_g", 0);
    profile_b[2] = preferences.getInt("p2_b", 255);
    profile_bright[2] = preferences.getInt("p2_br", 50);
    preferences.end();

    backupToRam();
    triggerBeep(35, 150);

    IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);
    
    current_render_brightness = led_is_on ? (float)brightness_pct : 0.0f;
    
    uint8_t tarR = COLOR_TEMPS[current_temp_index].r;
    uint8_t tarG = COLOR_TEMPS[current_temp_index].g;
    uint8_t tarB = COLOR_TEMPS[current_temp_index].b;
    if(custom_profile_active) {
        tarR = profile_r[selected_profile_idx]; tarG = profile_g[selected_profile_idx]; tarB = profile_b[selected_profile_idx];
    }
    for(int i=0; i<NEOPIXEL_COUNT; i++) {
        current_render_r[i] = tarR; current_render_g[i] = tarG; current_render_b[i] = tarB;
    }

    update_led_strip_buffer();
    
    xTaskCreatePinnedToCore(NetworkWorkerLoop, "NetworkTask", 6144, NULL, 1, &NetworkTaskHandle, 0);
    strip_needs_update = true;
}

void loop() {
    unsigned long now = millis();

    if (!monitor_armed && wifi_init_complete) {
        static unsigned long stable_start_time = 0;
        if (stable_start_time == 0) { stable_start_time = now; }

        if (now - stable_start_time >= 500) {
            long running_adc_total = 0;
            for (uint8_t i = 0; i < 30; i++) { running_adc_total += analogRead(POWER_SENSE_PIN); delay(2); }
            int baseline_wall_voltage = running_adc_total / 30;
            if (baseline_wall_voltage >= 500) {
                power_drop_threshold = baseline_wall_voltage - 150;
                monitor_armed = true; 
            }
        }
    }

    if (monitor_armed && analogRead(POWER_SENSE_PIN) < power_drop_threshold) {
        consecutive_drop_counter++;
        if (consecutive_drop_counter >= TRIGGER_CONSECUTIVE_LIMIT) {
            if (sleep_mode_active || timer_active) restoreFromRam();
            verifyAndSaveStateToFlash();
            ESP.restart(); 
        }
    } else {
        consecutive_drop_counter = 0;
    }

    if (timer_active) {
        if (now - timer_start_ms >= timer_duration_ms) {
            timer_active = false; 
            restoreFromRam();
            strip_needs_update = true; 
            for(int b=0; b<5; b++) { triggerBeep(500, 440); if(b < 4) delay(150); }
        }
    }

    if (sleep_mode_active && !timer_active) {
        if (now - sleep_start_ms >= SLEEP_DURATION_MS) {
            sleep_mode_active = false;
            restoreFromRam();
            led_is_on = false; 
            strip_needs_update = true;
        }
    }

    static bool last_button_state = true;
    static unsigned long button_press_start_time = 0;
    static bool warning_beep_triggered = false;
    static bool shutdown_executed = false;

    bool current_button_state = digitalRead(BUTTON_PIN);
    if (current_button_state == LOW) {
        if (last_button_state == HIGH) {
            button_press_start_time = now;
            warning_beep_triggered = false; shutdown_executed = false;
        }
        unsigned long press_duration = now - button_press_start_time;
        if (press_duration >= 2000 && !warning_beep_triggered) {
            triggerBeep(300, 180);
            warning_beep_triggered = true;
        }
        if (press_duration >= 3500 && !shutdown_executed) {
            shutdown_executed = true;
            led_is_on = false; 
            for(int i=0; i<NEOPIXEL_COUNT; i++) strip.setPixelColor(i,0);
            strip.show();
            if (sleep_mode_active || timer_active) { restoreFromRam(); led_is_on = false; }
            verifyAndSaveStateToFlash(); 
            esp_deep_sleep_start();
        }
    } else {
        if (last_button_state == LOW && !shutdown_executed) {
            unsigned long press_duration = now - button_press_start_time;
            if (press_duration > 50 && press_duration < 2000) {
                if (sleep_mode_active || timer_active) { sleep_mode_active = false; timer_active = false; restoreFromRam(); }
                led_is_on = !led_is_on;
                strip_needs_update = true;
                triggerBeep(35, 220); 
            }
        }
    }
    last_button_state = current_button_state;

    if (IrReceiver.decode()) {
        uint8_t cmd = 0xFF;
        bool action_taken = false; bool is_valid_packet = false;
        if (IrReceiver.decodedIRData.protocol == NEC) { cmd = IrReceiver.decodedIRData.command; last_valid_ir_cmd = cmd; is_valid_packet = true; } 
        else if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) { cmd = last_valid_ir_cmd; is_valid_packet = (cmd != 0xFF); }

        if (is_valid_packet) {
            switch (cmd) {
                case CMD_LIGHT:
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) { 
                        if(sleep_mode_active || timer_active) { sleep_mode_active = false; timer_active = false; restoreFromRam(); }
                        led_is_on = !led_is_on;
                        action_taken = true; 
                    }
                    break;
                case CMD_POWER:
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) { 
                        triggerBeep(200, 120);
                        if (sleep_mode_active || timer_active) restoreFromRam();
                        verifyAndSaveStateToFlash();
                        ESP.restart(); 
                    }
                    break;
                case CMD_SOUND:
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) broadcastIPAudio();
                    break;
                case CMD_PLUS:
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) && volume_mode < 2) { volume_mode++; action_taken = true; }
                    break;
                case CMD_MINUS:
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) && volume_mode > 0) { volume_mode--; action_taken = true; }
                    break;
                case CMD_LEFT:
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) && led_is_on && !timer_active) { 
                        custom_profile_active = false; rainbow_mode_active = false;
                        if (current_temp_index > 0) { current_temp_index--; action_taken = true; }
                        brightness_pct = COLOR_TEMPS[current_temp_index].default_bright;
                    }
                    break;
                case CMD_RIGHT:
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) && led_is_on && !timer_active) { 
                        custom_profile_active = false; rainbow_mode_active = false;
                        if (current_temp_index < TOTAL_TEMPS - 1) { current_temp_index++; action_taken = true; }
                        brightness_pct = COLOR_TEMPS[current_temp_index].default_bright;
                    }
                    break;
                case CMD_UP:
                    if (led_is_on && brightness_pct < 100) { 
                        brightness_pct = min(100, brightness_pct + BRIGHTNESS_STEP_PCT); 
                        if (custom_profile_active) profile_bright[selected_profile_idx] = brightness_pct;
                        action_taken = true; 
                    }
                    break;
                case CMD_DOWN:
                    if (led_is_on && brightness_pct > 0) { 
                        brightness_pct = max(0, brightness_pct - BRIGHTNESS_STEP_PCT); 
                        if(brightness_pct==0) led_is_on=false; 
                        if (custom_profile_active) profile_bright[selected_profile_idx] = brightness_pct;
                        action_taken = true; 
                    }
                    break;
                case CMD_ZERO: 
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) {
                        if (sleep_mode_active) { sleep_mode_active = false; restoreFromRam(); } 
                        else {
                            if(timer_active) { timer_active = false; restoreFromRam(); }
                            backupToRam(); brightness_pct = 25; led_is_on = true; sleep_mode_active = true; sleep_start_ms = now;
                        }
                        action_taken = true;
                    }
                    break;
                case CMD_ONE:   
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) { selected_profile_idx = 0; custom_profile_active = true; rainbow_mode_active = false; led_is_on = true; brightness_pct = profile_bright[0]; action_taken = true; }
                    break;
                case CMD_TWO:   
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) { selected_profile_idx = 1; custom_profile_active = true; rainbow_mode_active = false; led_is_on = true; brightness_pct = profile_bright[1]; action_taken = true; }
                    break;
                case CMD_THREE: 
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) { selected_profile_idx = 2; custom_profile_active = true; rainbow_mode_active = false; led_is_on = true; brightness_pct = profile_bright[2]; action_taken = true; }
                    break;
                case CMD_NINE:
                    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) { rainbow_mode_active = !rainbow_mode_active; custom_profile_active = false; led_is_on = rainbow_mode_active; brightness_pct = 50; action_taken = true; }
                    break;
            }
            if (action_taken) { 
                strip_needs_update = true;
                if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) triggerBeep(25, 240); 
            }
        }
        IrReceiver.resume();
    }

    if (now - last_fade_time >= FADE_INTERVAL) {
        last_fade_time = now;
        float target_render_brightness = led_is_on ? (float)brightness_pct : 0.0f;
        
        if (abs(current_render_brightness - target_render_brightness) > 0.05f) {
            current_render_brightness += (target_render_brightness - current_render_brightness) * FADE_SPEED_FACTOR;
            if (!led_is_on && current_render_brightness < 0.5f) current_render_brightness = 0.0f;
            strip_needs_update = true;
        } else {
            current_render_brightness = target_render_brightness;
        }

        update_led_strip_buffer();
        strip.show();
    }
}

void backupToRam() {
    ram_backup_cache.led_on = led_is_on;
    ram_backup_cache.bright_pct = brightness_pct;
    ram_backup_cache.temp_idx = current_temp_index;
    ram_backup_cache.custom_active = custom_profile_active;
    ram_backup_cache.rainbow_active = rainbow_mode_active;
    ram_backup_cache.custom_idx = selected_profile_idx;
}

void restoreFromRam() {
    led_is_on = ram_backup_cache.led_on;
    brightness_pct = ram_backup_cache.bright_pct;
    current_temp_index = ram_backup_cache.temp_idx;
    custom_profile_active = ram_backup_cache.custom_active;
    rainbow_mode_active = ram_backup_cache.rainbow_active;
    selected_profile_idx = ram_backup_cache.custom_idx;
}

uint32_t calculate_wheel_pos(uint8_t Position, float scalingFactor) {
    Position = 255 - Position;
    if(Position < 85) {
        return strip.Color((uint8_t)((255 - Position * 3) * scalingFactor), 0, (uint8_t)((Position * 3) * scalingFactor));
    }
    if(Position < 170) {
        Position -= 85;
        return strip.Color(0, (uint8_t)((Position * 3) * scalingFactor), (uint8_t)((255 - Position * 3) * scalingFactor));
    }
    Position -= 170;
    return strip.Color((uint8_t)((Position * 3) * scalingFactor), (uint8_t)((255 - Position * 3) * scalingFactor), 0);
}

void update_led_strip_buffer() {
    uint8_t mappedIntensityIndex = (uint8_t)constrain(current_render_brightness, 0, 100);
    uint8_t maxScaledDuty = pgm_read_byte(&CIE1931_CURVE[mappedIntensityIndex]);
    float multiplier = (float)maxScaledDuty / 255.0f;

    unsigned long t = millis();

    for (uint16_t i = 0; i < NEOPIXEL_COUNT; i++) {
        uint8_t targetR = 0, targetG = 0, targetB = 0;

        if (timer_active) {
            unsigned long elapsed = t - timer_start_ms;
            float progress = (float)elapsed / (float)timer_duration_ms;
            if (progress > 1.0f) progress = 1.0f;

            if (progress < 0.5f) {
                targetR = (uint8_t)(progress * 2.0f * 255.0f); targetG = 255;
            } else {
                targetR = 255; targetG = (uint8_t)((1.0f - progress) * 2.0f * 255.0f); 
            }
            targetB = 0;
        }
        else if (sleep_mode_active) {
            // A slow macro-phase shift to smoothly roll things across frames
            float slowTime = t * 0.0008f; 

            // Standard frequency oscillators run at higher multipliers to preserve mathematical step precision (No Choppiness)
            float waveA = sin(slowTime + (i * 0.5f));
            float waveB = cos((slowTime * 0.8f) - (i * 0.3f));
            float localizedScale = (waveA * waveB + 1.0f) * 0.5f; 

            // Sparsity threshold strictly tightened to thin out the number of active fireflies
            float roamingSparsityFilter = sin((slowTime * 0.2f) + (i * 0.4f)) * cos((slowTime * 0.15f) - (i * 0.2f));
            if (roamingSparsityFilter < 0.25f) { 
                localizedScale *= 0.05f; // Drop into a soft, unnoticeable background floor
            } else {
                localizedScale *= ((roamingSparsityFilter - 0.25f) * 1.33f); // Softly scale back up
            }

            // Global breathing intensity modulator that slowly fluctuates maximum brightness envelopes over time
            float breathingIntensity = (sin(t * 0.0003f) * 0.25f) + 0.75f; // Scales intensity between 50% and 100%
            localizedScale *= breathingIntensity;

            // Strict Amber & Soft Gold Flame palette (No Green tones allowed)
            float colorMapMod = sin((slowTime * 0.3f) + (i * 0.2f)) * 0.5f + 0.5f;
            if (colorMapMod < 0.5f) {
                float tBlend = colorMapMod / 0.5f;
                targetR = (uint8_t)(210 + (35 * tBlend)); // Smooth drift up to warm crimson-amber
                targetG = (uint8_t)(35 + (25 * tBlend));   // Kept strictly low to avoid green flaring
                targetB = 0;
            } else {
                float tBlend = (colorMapMod - 0.5f) / 0.5f;
                targetR = (uint8_t)(245 + (10 * tBlend)); // Soft gold ember shift
                targetG = (uint8_t)(60 + (30 * tBlend));   // Limited mid-range yellow floor
                targetB = (uint8_t)(2 * tBlend);
            }

            targetR = (uint8_t)(targetR * localizedScale);
            targetG = (uint8_t)(targetG * localizedScale);
            targetB = (uint8_t)(targetB * localizedScale);
        }
        else if (rainbow_mode_active) {
            uint16_t dynamicShift = (t / 20) & 0xFF;
            uint8_t elementPos = ((i * 256 / NEOPIXEL_COUNT) + dynamicShift) & 0xFF;
            uint32_t wheelColor = calculate_wheel_pos(elementPos, 1.0f);
            targetR = (wheelColor >> 16) & 0xFF;
            targetG = (wheelColor >> 8) & 0xFF;
            targetB = wheelColor & 0xFF;
        }
        else if (custom_profile_active) {
            targetR = profile_r[selected_profile_idx];
            targetG = profile_g[selected_profile_idx];
            targetB = profile_b[selected_profile_idx];
        }
        else {
            targetR = COLOR_TEMPS[current_temp_index].r;
            targetG = COLOR_TEMPS[current_temp_index].g;
            targetB = COLOR_TEMPS[current_temp_index].b;
        }

        current_render_r[i] += ((float)targetR - current_render_r[i]) * FADE_SPEED_FACTOR;
        current_render_g[i] += ((float)targetG - current_render_g[i]) * FADE_SPEED_FACTOR;
        current_render_b[i] += ((float)targetB - current_render_b[i]) * FADE_SPEED_FACTOR;

        strip.setPixelColor(i, strip.Color((uint8_t)(current_render_r[i] * multiplier), 
                                           (uint8_t)(current_render_g[i] * multiplier), 
                                           (uint8_t)(current_render_b[i] * multiplier)));
    }
}

void verifyAndSaveStateToFlash() {
    if (led_is_on == storage_cache.led_on && brightness_pct == storage_cache.bright_pct && 
        current_temp_index == storage_cache.temp_idx && volume_mode == storage_cache.vol_mode &&
        custom_profile_active == storage_cache.custom_active && rainbow_mode_active == storage_cache.rainbow_active &&
        selected_profile_idx == storage_cache.custom_idx) {
        return;
    }
    preferences.begin("lamp-state", false);
    preferences.putBool("led_on", led_is_on);
    preferences.putInt("bright_pct", brightness_pct);
    preferences.putInt("temp_idx", current_temp_index);
    preferences.putInt("vol_mode", volume_mode);
    preferences.putBool("cust_act", custom_profile_active);
    preferences.putBool("rbow_act", rainbow_mode_active);
    preferences.putInt("cust_idx", selected_profile_idx);
    preferences.end();
    storage_cache.led_on = led_is_on; storage_cache.bright_pct = brightness_pct;
    storage_cache.temp_idx = current_temp_index; storage_cache.vol_mode = volume_mode;
    storage_cache.custom_active = custom_profile_active; storage_cache.rainbow_active = rainbow_mode_active;
    storage_cache.custom_idx = selected_profile_idx;
}

void broadcastIPAudio() {
    String ipStr = WiFi.localIP().toString();
    IrReceiver.stop();
    triggerBeep(40, 280, true); delay(60); triggerBeep(40, 280, true);
    delay(600);
    for (uint8_t i = 0; i < ipStr.length(); i++) {
        char c = ipStr[i];
        if (c == '.') { triggerBeep(500, 160, true); } 
        else if (c == '0') { triggerBeep(30, 220, true); } 
        else if (c >= '1' && c <= '9') {
            int targetBeeps = c - '0';
            for (int b = 0; b < targetBeeps; b++) {
                triggerBeep(120, 220, true);
                if (b < targetBeeps - 1) delay(150);
            }
        }
        delay(600);
    }
    IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);
}