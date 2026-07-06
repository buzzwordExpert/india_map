// Header Declarations

#include <Arduino.h>
#include "BluetoothA2DPSource.h"
#include <SD.h>
#include <SPI.h>
#include <MFRC522.h>
#include <driver/i2s.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <algorithm>
#include <vector>
#include "esp_system.h"

// --- CONFIG ---
// #define DEBUG_165
#define PRESSED_ACTIVE_LOW 1

// --- Pin Definitions ---
#define PL   0
#define CP   18
#define Q7   19
#define LATCH 16
#define CLOCK 33
#define DATA  13
#define SD_CS   21
#define SD_SCK  14
#define SD_MISO 22
#define SD_MOSI 23
#define BT_LED      15
#define CORRECT_LED 32
#define WRONG_LED   12

// ============================================================
// BUTTON ASSIGNMENTS (74HC165 bit numbers)
// ============================================================
#define QUIZ_BUTTON_NUM  36   
#define RESET_BUTTON_NUM 37   
#define MODE_BUTTON_NUM  38   
#define LANG_BUTTON_NUM  39     

#define I2S_BCLK 26
#define I2S_LRCK 25
#define I2S_DOUT 27

// RFID Pins
#define RFID_RST  4
#define RFID_SS   2
#define RFID_SCK  5
#define RFID_MOSI 17
#define RFID_MISO 35

// --- Shift register chain (74HC595, output side) ---
#define NUM_595_CHIPS 5
#define LED_ALL_OFF 0xFFFFFFFFFFULL
#define LED_ALL_ON  0x0000000000ULL

BluetoothA2DPSource a2dp_source;
SPIClass sdSPI(HSPI);
MFRC522  mfrc(RFID_SS, RFID_RST);
WebServer setupServer(80);
DNSServer setupDns;

const char* DEFAULT_BT_NAME       = "OnePlus Nord Buds 2";
char        btSpeakerName[64]     = "OnePlus Nord Buds 2";
const char* SPEAKER_CONFIG_FILE   = "/speaker.txt";
const char* SETUP_AP_SSID         = "Bharat-Setup";
const unsigned long BOSS_HOLD_TIME = 3000;   
const byte DNS_PORT = 53;

// =====================================================
// LANGUAGE SYSTEM  (Hindi=0, English=1)
// =====================================================
enum SystemLanguage {
  SYS_LANG_HINDI   = 0,
  SYS_LANG_ENGLISH = 1,
  SYS_LANG_COUNT   = 2
};
SystemLanguage currentLanguage = SYS_LANG_HINDI;
bool setupPortalSaved = false;

const char* const languageNames[SYS_LANG_COUNT] = { "Hindi", "English" };
const char* const languageFolders[SYS_LANG_COUNT] = { "Hindi", "English" };
const char* const questionSuffixes[SYS_LANG_COUNT] = { "h", "e" };

String getLocalizedAudio(const char* const paths[], int langCount = SYS_LANG_COUNT) {
  int lang = (int)currentLanguage;
  if (lang >= langCount) lang = 0;          
  if (paths[lang]) return String(paths[lang]);
  if (paths[0])    return String(paths[0]); 
  return String("");
}

// =====================================================
// AUDIO FORMAT GLOBALS
// =====================================================
uint32_t dataSize     = 0;
uint32_t sampleRate   = 44100;
uint16_t numChannels  = 2;
uint16_t bitsPerSample = 16;

// =====================================================
// RUNTIME FLAGS & BUFFERS
// =====================================================
QueueHandle_t buttonQueue;
volatile bool bluetoothMode = true;
volatile bool streamPaused  = true;
volatile bool localPaused   = true;
uint64_t ledState = LED_ALL_OFF;
bool rfidMode        = false;
bool rfidInitialized = false;

// Global audio read buffer to prevent stack overflows and allow fast 1-sweep reads
uint8_t wavReadBuffer[4096];

// =====================================================
// STATE AUDIO — 36 entries × 2 languages
// =====================================================
const char* const switchToAudio[36][SYS_LANG_COUNT] = {
  {"/Hindi/Uttar Pradesh.wav",                    "/English/Uttar Pradesh_eng.wav"},
  {"/Hindi/Rajasthan.wav",                        "/English/Rajasthan_eng.wav"},
  {"/Hindi/Bihar.wav",                            "/English/Bihar_eng.wav"},
  {"/Hindi/Sikkim.wav",                           "/English/Sikkim_eng.wav"},
  {"/Hindi/Himachal Pradesh.wav",                 "/English/Himachal_eng.wav"},
  {"/Hindi/Punjab.wav",                           "/English/Punjab_eng.wav"},
  {"/Hindi/Uttarakhand.wav",                      "/English/Uttarakhand_eng.wav"},
  {"/Hindi/Haryana.wav",                          "/English/Haryana_eng.wav"},
  {"/Hindi/Tripura.wav",                          "/English/Tripura_eng.wav"},
  {"/Hindi/Meghalaya.wav",                        "/English/Meghalaya_eng.wav"},
  {"/Hindi/Assam.wav",                             "/English/Assam_eng.wav"},
  {"/Hindi/West Bengal.wav",                      "/English/West Bengal_eng.wav"},
  {"/Hindi/Arunachal Pradesh.wav",                "/English/Arunachal Pradesh_eng.wav"},
  {"/Hindi/Nagaland.wav",                         "/English/Nagaland_eng.wav"},
  {"/Hindi/Manipur.wav",                          "/English/Manipur_eng.wav"},
  {"/Hindi/Mizoram.wav",                          "/English/Mizoram_eng.wav"},
  {"/Hindi/Jharkhand.wav",                        "/English/Jharkhand_eng.wav"},
  {"/Hindi/Chattisgarh.wav",                      "/English/Chhattisgarh_eng.wav"},
  {"/Hindi/Madhya Pradesh.wav",                   "/English/Madhya Pradesh_eng.wav"},
  {"/Hindi/Gujarat.wav",                          "/English/Gujarat_eng.wav"},
  {"/Hindi/Odisha.wav",                           "/English/Odisha_eng.wav"},
  {"/Hindi/Maharashtra.wav",                      "/English/Maharashtra_eng.wav"},
  {"/Hindi/Telangana.wav",                        "/English/Telangana_eng.wav"},
  {"/Hindi/Andhra Pradesh.wav",                   "/English/Andhra Pradesh_eng.wav"},
  {"/Hindi/Goa.wav",                              "/English/Goa_eng.wav"},
  {"/Hindi/Karnataka.wav",                        "/English/Karnataka_eng.wav"},
  {"/Hindi/Kerala.wav",                           "/English/Kerala_eng.wav"},
  {"/Hindi/Tamil Nadu.wav",                       "/English/Tamil Nadu_eng.wav"},
  {"/Hindi/Ladakh.wav",                           "/English/Ladakh_eng.wav"},
  {"/Hindi/Jammu and Kashmir.wav",                "/English/Jammu and Kashmir_eng.wav"},
  {"/Hindi/Chandigarh.wav",                       "/English/Chandigarh_eng.wav"},
  {"/Hindi/Delhi.wav",                            "/English/Delhi_eng.wav"},
  {"/Hindi/Lakshwadeep.wav",                      "/English/Lakshadweep_eng.wav"},
  {"/Hindi/Andaman and Nicobar.wav",              "/English/Andaman_eng.wav"},
  {"/Hindi/Dadar and Nagar Haveli and Daman and Diu.wav", "/English/Dadar and Nagar Haveli and Daman and Diu_eng.wav"},
  {"/Hindi/Puduchery.wav",                        "/English/Puducherry_eng.wav"}
};

String getSwitchAudioPath(int index) {
  if (index < 0 || index >= 36) return String("");
  return getLocalizedAudio(switchToAudio[index]);
}

// =====================================================
// WRONG-ANSWER "CORRECT ANSWER IS…"
// =====================================================
const char* const correctAnnouncement[36][SYS_LANG_COUNT] = {
  {"/Hindi/up_wrong.wav",     "/English/up_wrong.wav"},
  {"/Hindi/raj_wrong.wav",    "/English/raj_wrong.wav"},
  {"/Hindi/bih_wrong.wav",    "/English/bih_wrong.wav"},
  {"/Hindi/sik_wrong.wav",    "/English/sik_wrong.wav"},
  {"/Hindi/hp_wrong.wav",     "/English/hp_wrong.wav"},
  {"/Hindi/pun_wrong.wav",    "/English/pun_wrong.wav"},
  {"/Hindi/uk_wrong.wav",     "/English/uk_wrong.wav"},
  {"/Hindi/har_wrong.wav",    "/English/har_wrong.wav"},
  {"/Hindi/tri_wrong.wav",    "/English/tri_wrong.wav"},
  {"/Hindi/meg_wrong.wav",    "/English/meg_wrong.wav"},
  {"/Hindi/ass_wrong.wav",    "/English/ass_wrong.wav"},
  {"/Hindi/wb_wrong.wav",     "/English/wb_wrong.wav"},
  {"/Hindi/aru_wrong.wav",    "/English/aru_wrong.wav"},
  {"/Hindi/nag_wrong.wav",    "/English/nag_wrong.wav"},
  {"/Hindi/man_wrong.wav",    "/English/man_wrong.wav"},
  {"/Hindi/miz_wrong.wav",    "/English/miz_wrong.wav"},
  {"/Hindi/jha_wrong.wav",    "/English/jha_wrong.wav"},
  {"/Hindi/chh_wrong.wav",    "/English/chh_wrong.wav"},
  {"/Hindi/mp_wrong.wav",     "/English/mp_wrong.wav"},
  {"/Hindi/guj_wrong.wav",    "/English/guj_wrong.wav"},
  {"/Hindi/odi_wrong.wav",    "/English/odi_wrong.wav"},
  {"/Hindi/mah_wrong.wav",    "/English/mah_wrong.wav"},
  {"/Hindi/tel_wrong.wav",    "/English/tel_wrong.wav"},
  {"/Hindi/ap_wrong.wav",     "/English/ap_wrong.wav"},
  {"/Hindi/goa_wrong.wav",    "/English/goa_wrong.wav"},
  {"/Hindi/kar_wrong.wav",    "/English/kar_wrong.wav"},
  {"/Hindi/ker_wrong.wav",    "/English/ker_wrong.wav"},
  {"/Hindi/tn_wrong.wav",     "/English/tn_wrong.wav"},
  {"/Hindi/lad_wrong.wav",    "/English/lad_wrong.wav"},
  {"/Hindi/jk_wrong.wav",     "/English/jk_wrong.wav"},
  {"/Hindi/chd_wrong.wav",    "/English/chd_wrong.wav"},
  {"/Hindi/del_wrong.wav",    "/English/del_wrong.wav"},
  {"/Hindi/lak_wrong.wav",    "/English/lak_wrong.wav"},
  {"/Hindi/an_wrong.wav",     "/English/an_wrong.wav"},
  {"/Hindi/dnhdd_wrong.wav",  "/English/dnhdd_wrong.wav"},
  {"/Hindi/pud_wrong.wav",    "/English/pud_wrong.wav"}
};

String getCorrectAnnouncementPath(int index) {
  if (index < 0 || index >= 36) return String("");
  return getLocalizedAudio(correctAnnouncement[index]);
}

// =====================================================
// SWITCH → LED MAPPING (5x 74HC595, 36 unique outputs)
// =====================================================
const int8_t switchToLED[36] = {
  15,  0, 23, 24,  5, 32,  7,  2, 26, 25, 30, 22, 31, 29, 28, 27,
  21, 13, 12,  8, 14, 10, 11, 34,  9, 16, 18, 19,  6,  1,  3,  4,
  17, 20, 33, 35
};

// =====================================================
// MISC AUDIO
// =====================================================
const char* const correctAudioPaths[SYS_LANG_COUNT] = { "/Hindi/correct_hindi.wav", "/English/correct1_eng.wav" };
const char* const wrongAudioPaths[SYS_LANG_COUNT] = { "/Hindi/wrong.wav", "/English/wrong_eng.wav" };
const char* const instructionsAudioPaths[SYS_LANG_COUNT] = { "/Hindi/instructions.wav", "/English/instructions.wav" };
const char* const rfidPlayModeAudioPaths[SYS_LANG_COUNT] = { "/Hindi/rfid_hindi_mode.wav", "/English/rfid_play_mode.wav" };
const char* const rfidQuizModeAudioPaths[SYS_LANG_COUNT] = { "/Hindi/rfid_hindi_mode.wav", "/English/rfid_quiz_mode.wav" };
const char* const switchPlayModeAudioPaths[SYS_LANG_COUNT] = { "/Hindi/switch_play_mode.wav", "/English/switch_play_mode.wav" };
const char* const switchQuizModeAudioPaths[SYS_LANG_COUNT] = { "/Hindi/switch_quiz_mode.wav", "/English/switch_quiz_mode.wav" };
const char* const welcomeAudioPaths[SYS_LANG_COUNT] = { "/Hindi/welcome.wav", "/English/welcome.wav" };
const char* const mlSwitchQuizAudioPaths[SYS_LANG_COUNT] = { "/Hindi/mlswitch_hindi.wav", "/English/switch_quiz_mode.wav" };
const char* const mlRfidQuizAudioPaths[SYS_LANG_COUNT] = { "/Hindi/rfid_quiz_mode_hindi.wav", "/English/rfid_quiz_mode.wav" };
const char* const resetAudioPaths[SYS_LANG_COUNT] = { "/Hindi/reset_h.wav", "/English/reset.wav" };
const char* const languageChangedAudioPaths[SYS_LANG_COUNT] = { "/Hindi/Hindi_lan.wav", "/English/English_lan.wav" };
const char* const scoreIntroAudioPaths[SYS_LANG_COUNT] = { "/Hindi/final_score_hindi.wav", "/English/final_score_hindi.wav" };
const char* const outOfAudioPaths[SYS_LANG_COUNT] = { "/Hindi/outof_hindi.wav", "/English/outof.wav" };

const char* correctWordAudio = "/correctword.wav";

// =====================================================
// NUMBER AUDIO
// =====================================================
const char* numberAudioFiles[401] = {
    "/Hindi/n0.wav", "/Hindi/1.wav", "/Hindi/2.wav", "/Hindi/3.wav", "/Hindi/4.wav",
    "/Hindi/5.wav", "/Hindi/6.wav", "/Hindi/7.wav", "/Hindi/8.wav", "/Hindi/9.wav",
    "/Hindi/10.wav", "/Hindi/11.wav", "/Hindi/12.wav", "/Hindi/13.wav", "/Hindi/14.wav",
    "/Hindi/15.wav", "/Hindi/16.wav", "/Hindi/17.wav", "/Hindi/18.wav", "/Hindi/19.wav",
    "/Hindi/20.wav", "/Hindi/21.wav", "/Hindi/22.wav", "/Hindi/23.wav", "/Hindi/24.wav",
    "/Hindi/25.wav", "/Hindi/26.wav", "/Hindi/27.wav", "/Hindi/28.wav", "/Hindi/29.wav",
    "/Hindi/30.wav", "/Hindi/31.wav", "/Hindi/32.wav", "/Hindi/33.wav", "/Hindi/34.wav",
    "/Hindi/35.wav", "/Hindi/36.wav", "/Hindi/37.wav", "/Hindi/38.wav", "/Hindi/39.wav",
    "/Hindi/40.wav", "/Hindi/41.wav", "/Hindi/42.wav", "/Hindi/43.wav", "/Hindi/44.wav",
    "/Hindi/45.wav", "/Hindi/46.wav", "/Hindi/47.wav", "/Hindi/48.wav", "/Hindi/49.wav",
    "/Hindi/50.wav", "/Hindi/51.wav", "/Hindi/52.wav", "/Hindi/53.wav", "/Hindi/54.wav",
    "/Hindi/55.wav", "/Hindi/56.wav", "/Hindi/57.wav", "/Hindi/58.wav", "/Hindi/59.wav",
    "/Hindi/60.wav", "/Hindi/61.wav", "/Hindi/62.wav", "/Hindi/63.wav", "/Hindi/64.wav",
    "/Hindi/65.wav", "/Hindi/66.wav", "/Hindi/67.wav", "/Hindi/68.wav", "/Hindi/69.wav",
    "/Hindi/70.wav", "/Hindi/71.wav", "/Hindi/72.wav", "/Hindi/73.wav", "/Hindi/74.wav",
    "/Hindi/75.wav", "/Hindi/76.wav", "/Hindi/77.wav", "/Hindi/78.wav", "/Hindi/79.wav",
    "/Hindi/80.wav", "/Hindi/81.wav", "/Hindi/82.wav", "/Hindi/83.wav", "/Hindi/84.wav",
    "/Hindi/85.wav", "/Hindi/86.wav", "/Hindi/87.wav", "/Hindi/88.wav", "/Hindi/89.wav",
    "/Hindi/90.wav", "/Hindi/91.wav", "/Hindi/92.wav", "/Hindi/93.wav", "/Hindi/94.wav",
    "/Hindi/95.wav", "/Hindi/96.wav", "/Hindi/97.wav", "/Hindi/98.wav", "/Hindi/99.wav",
    "/Hindi/100.wav", "/Hindi/101.wav", "/Hindi/102.wav", "/Hindi/103.wav", "/Hindi/104.wav",
    "/Hindi/105.wav", "/Hindi/106.wav", "/Hindi/107.wav", "/Hindi/108.wav", "/Hindi/109.wav",
    "/Hindi/110.wav", "/Hindi/111.wav", "/Hindi/112.wav", "/Hindi/113.wav", "/Hindi/114.wav",
    "/Hindi/115.wav", "/Hindi/116.wav", "/Hindi/117.wav", "/Hindi/118.wav", "/Hindi/119.wav",
    "/Hindi/120.wav", "/Hindi/121.wav", "/Hindi/122.wav", "/Hindi/123.wav", "/Hindi/124.wav",
    "/Hindi/125.wav", "/Hindi/126.wav", "/Hindi/127.wav", "/Hindi/128.wav", "/Hindi/129.wav",
    "/Hindi/130.wav", "/Hindi/131.wav", "/Hindi/132.wav", "/Hindi/133.wav", "/Hindi/134.wav",
    "/Hindi/135.wav", "/Hindi/136.wav", "/Hindi/137.wav", "/Hindi/138.wav", "/Hindi/139.wav",
    "/Hindi/140.wav", "/Hindi/141.wav", "/Hindi/142.wav", "/Hindi/143.wav", "/Hindi/144.wav",
    "/Hindi/145.wav", "/Hindi/146.wav", "/Hindi/147.wav", "/Hindi/148.wav", "/Hindi/149.wav",
    "/Hindi/150.wav", "/Hindi/151.wav", "/Hindi/152.wav", "/Hindi/153.wav", "/Hindi/154.wav",
    "/Hindi/155.wav", "/Hindi/156.wav", "/Hindi/157.wav", "/Hindi/158.wav", "/Hindi/159.wav",
    "/Hindi/160.wav", "/Hindi/161.wav", "/Hindi/162.wav", "/Hindi/163.wav", "/Hindi/164.wav",
    "/Hindi/165.wav", "/Hindi/166.wav", "/Hindi/167.wav", "/Hindi/168.wav", "/Hindi/169.wav",
    "/Hindi/170.wav", "/Hindi/171.wav", "/Hindi/172.wav", "/Hindi/173.wav", "/Hindi/174.wav",
    "/Hindi/175.wav", "/Hindi/176.wav", "/Hindi/177.wav", "/Hindi/178.wav", "/Hindi/179.wav",
    "/Hindi/180.wav", "/Hindi/181.wav", "/Hindi/182.wav", "/Hindi/183.wav", "/Hindi/184.wav",
    "/Hindi/185.wav", "/Hindi/186.wav", "/Hindi/187.wav", "/Hindi/188.wav", "/Hindi/189.wav",
    "/Hindi/190.wav", "/Hindi/191.wav", "/Hindi/192.wav", "/Hindi/193.wav", "/Hindi/194.wav",
    "/Hindi/195.wav", "/Hindi/196.wav", "/Hindi/197.wav", "/Hindi/198.wav", "/Hindi/199.wav",
    "/Hindi/200.wav", "/Hindi/201.wav", "/Hindi/202.wav", "/Hindi/203.wav", "/Hindi/204.wav",
    "/Hindi/205.wav", "/Hindi/206.wav", "/Hindi/207.wav", "/Hindi/208.wav", "/Hindi/209.wav",
    "/Hindi/210.wav", "/Hindi/211.wav", "/Hindi/212.wav", "/Hindi/213.wav", "/Hindi/214.wav",
    "/Hindi/215.wav", "/Hindi/216.wav", "/Hindi/217.wav", "/Hindi/218.wav", "/Hindi/219.wav",
    "/Hindi/220.wav", "/Hindi/221.wav", "/Hindi/222.wav", "/Hindi/223.wav", "/Hindi/224.wav",
    "/Hindi/225.wav", "/Hindi/226.wav", "/Hindi/227.wav", "/Hindi/228.wav", "/Hindi/229.wav",
    "/Hindi/230.wav", "/Hindi/231.wav", "/Hindi/232.wav", "/Hindi/233.wav", "/Hindi/234.wav",
    "/Hindi/235.wav", "/Hindi/236.wav", "/Hindi/237.wav", "/Hindi/238.wav", "/Hindi/239.wav",
    "/Hindi/240.wav", "/Hindi/241.wav", "/Hindi/242.wav", "/Hindi/243.wav", "/Hindi/244.wav",
    "/Hindi/245.wav", "/Hindi/246.wav", "/Hindi/247.wav", "/Hindi/248.wav", "/Hindi/249.wav",
    "/Hindi/250.wav", "/Hindi/251.wav", "/Hindi/252.wav", "/Hindi/253.wav", "/Hindi/254.wav",
    "/Hindi/255.wav", "/Hindi/256.wav", "/Hindi/257.wav", "/Hindi/258.wav", "/Hindi/259.wav",
    "/Hindi/260.wav", "/Hindi/261.wav", "/Hindi/262.wav", "/Hindi/263.wav", "/Hindi/264.wav",
    "/Hindi/265.wav", "/Hindi/266.wav", "/Hindi/267.wav", "/Hindi/268.wav", "/Hindi/269.wav",
    "/Hindi/270.wav", "/Hindi/271.wav", "/Hindi/272.wav", "/Hindi/273.wav", "/Hindi/274.wav",
    "/Hindi/275.wav", "/Hindi/276.wav", "/Hindi/277.wav", "/Hindi/278.wav", "/Hindi/279.wav",
    "/Hindi/280.wav", "/Hindi/281.wav", "/Hindi/282.wav", "/Hindi/283.wav", "/Hindi/284.wav",
    "/Hindi/285.wav", "/Hindi/286.wav", "/Hindi/287.wav", "/Hindi/288.wav", "/Hindi/289.wav",
    "/Hindi/290.wav", "/Hindi/291.wav", "/Hindi/292.wav", "/Hindi/293.wav", "/Hindi/294.wav",
    "/Hindi/295.wav", "/Hindi/296.wav", "/Hindi/297.wav", "/Hindi/298.wav", "/Hindi/299.wav",
    "/Hindi/300.wav", "/Hindi/301.wav", "/Hindi/302.wav", "/Hindi/303.wav", "/Hindi/304.wav",
    "/Hindi/305.wav", "/Hindi/306.wav", "/Hindi/307.wav", "/Hindi/308.wav", "/Hindi/309.wav",
    "/Hindi/310.wav", "/Hindi/311.wav", "/Hindi/312.wav", "/Hindi/313.wav", "/Hindi/314.wav",
    "/Hindi/315.wav", "/Hindi/316.wav", "/Hindi/317.wav", "/Hindi/318.wav", "/Hindi/319.wav",
    "/Hindi/320.wav", "/Hindi/321.wav", "/Hindi/322.wav", "/Hindi/323.wav", "/Hindi/324.wav",
    "/Hindi/325.wav", "/Hindi/326.wav", "/Hindi/327.wav", "/Hindi/328.wav", "/Hindi/329.wav",
    "/Hindi/330.wav", "/Hindi/331.wav", "/Hindi/332.wav", "/Hindi/333.wav", "/Hindi/334.wav",
    "/Hindi/335.wav", "/Hindi/336.wav", "/Hindi/337.wav", "/Hindi/338.wav", "/Hindi/339.wav",
    "/Hindi/340.wav", "/Hindi/341.wav", "/Hindi/342.wav", "/Hindi/343.wav", "/Hindi/344.wav",
    "/Hindi/345.wav", "/Hindi/346.wav", "/Hindi/347.wav", "/Hindi/348.wav", "/Hindi/349.wav",
    "/Hindi/350.wav", "/Hindi/351.wav", "/Hindi/352.wav", "/Hindi/353.wav", "/Hindi/354.wav",
    "/Hindi/355.wav", "/Hindi/356.wav", "/Hindi/357.wav", "/Hindi/358.wav", "/Hindi/359.wav",
    "/Hindi/360.wav", "/Hindi/361.wav", "/Hindi/362.wav", "/Hindi/363.wav", "/Hindi/364.wav",
    "/Hindi/365.wav", "/Hindi/366.wav", "/Hindi/367.wav", "/Hindi/368.wav", "/Hindi/369.wav",
    "/Hindi/370.wav", "/Hindi/371.wav", "/Hindi/372.wav", "/Hindi/373.wav", "/Hindi/374.wav",
    "/Hindi/375.wav", "/Hindi/376.wav", "/Hindi/377.wav", "/Hindi/378.wav", "/Hindi/379.wav",
    "/Hindi/380.wav", "/Hindi/381.wav", "/Hindi/382.wav", "/Hindi/383.wav", "/Hindi/384.wav",
    "/Hindi/385.wav", "/Hindi/386.wav", "/Hindi/387.wav", "/Hindi/388.wav", "/Hindi/389.wav",
    "/Hindi/390.wav", "/Hindi/391.wav", "/Hindi/392.wav", "/Hindi/393.wav", "/Hindi/394.wav",
    "/Hindi/395.wav", "/Hindi/396.wav", "/Hindi/397.wav", "/Hindi/398.wav", "/Hindi/399.wav",
    "/Hindi/400.wav"
};

String getNumberAudioPath(int n) {
  if (n < 0 || n > 401) return String("");
  const char* hindiPath = numberAudioFiles[n];
  if (currentLanguage == SYS_LANG_HINDI) return String(hindiPath);
  String ep = String("/") + languageFolders[currentLanguage] + "/n" + String(n) + ".wav";
  if (SD.exists(ep.c_str())) return ep;
  return String(hindiPath);  
}

// =====================================================
// QUIZ ANSWER KEY
// =====================================================
const std::vector<int8_t> correctAnswers[87] = { 
  // Original 87 Questions
  {23},{12},{10},{2},{17},{24},{19},{7,5},{11},{4},      // Q1–10
  {16},{25},{26},{18},{21},{14},{9},{15},{13},{20},      // Q11–20
  {5,7},{1},{3},{27},{22},{8},{0},{6},{0},{5},           // Q21–30
  {10},{19},{22},{25},{20},{20},{2},{2},{9},{14},        // Q31–40
  {12},{3},{25},{21},{21},{27},{19},{6},{10},{2},        // Q41–50
  {26},{27},{13},{5},{26},{27},{14},{17},{25},{6},       // Q51–60
  {12},{9},{5},{27},{30},{4},{26},{8},{30},{5},          // Q61–70
  {28},{33},{31},{29},{28},{32},{33},{35},{34},{31},     // Q71–80
  {21},{1},{6},{11},{33},{35},{28},                       // Q81–87                                    
};

const uint8_t TOTAL_QUESTIONS = 87;
bool questionsAsked[87]  = {false};
int  totalQuestionsAsked = 0;

// =====================================================
// QUIZ QUESTIONS — 87 entries × 2 languages
// =====================================================
const char* const quizQuestions[87][SYS_LANG_COUNT] = {
  {"/Hindi/q1h.wav",  "/English/q1e.wav"},  {"/Hindi/q2h.wav",  "/English/q2e.wav"},
  {"/Hindi/q3h.wav",  "/English/q3e.wav"},  {"/Hindi/q4h.wav",  "/English/q4e.wav"},
  {"/Hindi/q5h.wav",  "/English/q5e.wav"},  {"/Hindi/q6h.wav",  "/English/q6e.wav"},
  {"/Hindi/q7h.wav",  "/English/q7e.wav"},  {"/Hindi/q8h.wav",  "/English/q8e.wav"},
  {"/Hindi/q9h.wav",  "/English/q9e.wav"},  {"/Hindi/q10h.wav", "/English/q10e.wav"},
  {"/Hindi/q11h.wav", "/English/q11e.wav"}, {"/Hindi/q12h.wav", "/English/q12e.wav"},
  {"/Hindi/q13h.wav", "/English/q13e.wav"}, {"/Hindi/q14h.wav", "/English/q14e.wav"},
  {"/Hindi/q15h.wav", "/English/q15e.wav"}, {"/Hindi/q16h.wav", "/English/q16e.wav"},
  {"/Hindi/q17h.wav", "/English/q17e.wav"}, {"/Hindi/q18h.wav", "/English/q18e.wav"},
  {"/Hindi/q19h.wav", "/English/q19e.wav"}, {"/Hindi/q20h.wav", "/English/q20e.wav"},
  {"/Hindi/q21h.wav", "/English/q21e.wav"}, {"/Hindi/q22h.wav", "/English/q22e.wav"},
  {"/Hindi/q23h.wav", "/English/q23e.wav"}, {"/Hindi/q24h.wav", "/English/q24e.wav"},
  {"/Hindi/q25h.wav", "/English/q25e.wav"}, {"/Hindi/q26h.wav", "/English/q26e.wav"},
  {"/Hindi/q27h.wav", "/English/q27e.wav"}, {"/Hindi/q28h.wav", "/English/q28e.wav"},
  {"/Hindi/q29h.wav", "/English/q29e.wav"}, {"/Hindi/q30h.wav", "/English/q30e.wav"},
  {"/Hindi/q31h.wav", "/English/q31e.wav"}, {"/Hindi/q32h.wav", "/English/q32e.wav"},
  {"/Hindi/q33h.wav", "/English/q33e.wav"}, {"/Hindi/q34h.wav", "/English/q34e.wav"},
  {"/Hindi/q35h.wav", "/English/q35e.wav"}, {"/Hindi/q36h.wav", "/English/q36e.wav"},
  {"/Hindi/q37h.wav", "/English/q37e.wav"}, {"/Hindi/q38h.wav", "/English/q38e.wav"},
  {"/Hindi/q39h.wav", "/English/q39e.wav"}, {"/Hindi/q40h.wav", "/English/q40e.wav"},
  {"/Hindi/q41h.wav", "/English/q41e.wav"}, {"/Hindi/q42h.wav", "/English/q42e.wav"},
  {"/Hindi/q43h.wav", "/English/q43e.wav"}, {"/Hindi/q44h.wav", "/English/q44e.wav"},
  {"/Hindi/q45h.wav", "/English/q45e.wav"}, {"/Hindi/q46h.wav", "/English/q46e.wav"},
  {"/Hindi/q47h.wav", "/English/q47e.wav"}, {"/Hindi/q48h.wav", "/English/q48e.wav"},
  {"/Hindi/q49h.wav", "/English/q49e.wav"}, {"/Hindi/q50h.wav", "/English/q50e.wav"},
  {"/Hindi/q51h.wav", "/English/q51e.wav"}, {"/Hindi/q52h.wav", "/English/q52e.wav"},
  {"/Hindi/q53h.wav", "/English/q53e.wav"}, {"/Hindi/q54h.wav", "/English/q54e.wav"},
  {"/Hindi/q55h.wav", "/English/q55e.wav"}, {"/Hindi/q56h.wav", "/English/q56e.wav"},
  {"/Hindi/q57h.wav", "/English/q57e.wav"}, {"/Hindi/q58h.wav", "/English/q58e.wav"},
  {"/Hindi/q59h.wav", "/English/q59e.wav"}, {"/Hindi/q60h.wav", "/English/q60e.wav"},
  {"/Hindi/q61h.wav", "/English/q61e.wav"}, {"/Hindi/q62h.wav", "/English/q62e.wav"},
  {"/Hindi/q63h.wav", "/English/q63e.wav"}, {"/Hindi/q64h.wav", "/English/q64e.wav"},
  {"/Hindi/q65h.wav", "/English/q65e.wav"}, {"/Hindi/q66h.wav", "/English/q66e.wav"},
  {"/Hindi/q67h.wav", "/English/q67e.wav"}, {"/Hindi/q68h.wav", "/English/q68e.wav"},
  {"/Hindi/q69h.wav", "/English/q69e.wav"}, {"/Hindi/q70h.wav", "/English/q70e.wav"},
  {"/Hindi/q71h.wav", "/English/q71e.wav"}, {"/Hindi/q72h.wav", "/English/q72e.wav"},
  {"/Hindi/q73h.wav", "/English/q73e.wav"}, {"/Hindi/q74h.wav", "/English/q74e.wav"},
  {"/Hindi/q75h.wav", "/English/q75e.wav"}, {"/Hindi/q76h.wav", "/English/q76e.wav"},
  {"/Hindi/q77h.wav", "/English/q77e.wav"}, {"/Hindi/q78h.wav", "/English/q78e.wav"},
  {"/Hindi/q79h.wav", "/English/q79e.wav"}, {"/Hindi/q80h.wav", "/English/q80e.wav"},
  {"/Hindi/q81h.wav", "/English/q81e.wav"}, {"/Hindi/q82h.wav", "/English/q82e.wav"},
  {"/Hindi/q83h.wav", "/English/q83e.wav"}, {"/Hindi/q84h.wav", "/English/q84e.wav"},
  {"/Hindi/q85h.wav", "/English/q85e.wav"}, {"/Hindi/q86h.wav", "/English/q86e.wav"},
  {"/Hindi/q87h.wav", "/English/q87e.wav"},
};

String getQuizQuestionPath(int index) {
  if (index < 0 || index >= TOTAL_QUESTIONS) return String("");
  return getLocalizedAudio(quizQuestions[index]);
}

bool isCorrectAnswer(int8_t answer, int questionIndex) {
  const std::vector<int8_t>& v = correctAnswers[questionIndex];
  return std::find(v.begin(), v.end(), answer) != v.end();
}
int8_t getFirstCorrectAnswer(int questionIndex) {
  return correctAnswers[questionIndex][0];
}

int findNextSequentialQuestion() {
  for (int i = 0; i < TOTAL_QUESTIONS; i++) {
    if (!questionsAsked[i]) return i;
  }
  return -1;
}

// =====================================================
// QUIZ STATE MACHINE
// =====================================================
enum QuizState {
  QUIZ_IDLE,
  QUIZ_PLAYING_QUESTION,
  QUIZ_WAITING_FOR_ANSWER,
  QUIZ_PLAYING_FEEDBACK,
  QUIZ_PLAYING_CORRECT_ANSWER,
  QUIZ_QUESTION_COMPLETE,
  QUIZ_PLAYING_SCORE
};
QuizState quizState = QUIZ_IDLE;
bool quizMode = false;

// =====================================================
// DEFERRED-ACTION QUEUE
// =====================================================
enum PendingAction {
  PENDING_NONE = 0,
  PENDING_PLAY_QUESTION_AFTER_INSTRUCTIONS,
  PENDING_CONTINUE_SCORE,
  PENDING_PLAY_QUESTION_IDLE,
  PENDING_NEXT_QUESTION_DELAYED,      
  PENDING_PLAY_CORRECT_ANNOUNCEMENT,  
  PENDING_NEXT_QUESTION_AFTER_CORRECT_ANNOUNCEMENT,
  PENDING_MODE_ANNOUNCEMENT
};
volatile PendingAction pendingAction     = PENDING_NONE;
volatile unsigned long pendingActionAt   = 0;
volatile int8_t        pendingCorrectIdx = -1;

String getModeAnnouncement() {
  if (quizMode)
    return getLocalizedAudio(rfidMode ? mlRfidQuizAudioPaths : mlSwitchQuizAudioPaths);
  return getLocalizedAudio(rfidMode ? rfidPlayModeAudioPaths : switchPlayModeAudioPaths);
}

// =====================================================
// SCORE SEQUENCE
// =====================================================
enum ScoreSequence {
  SEQ_NONE, SEQ_SCORE_INTRO, SEQ_CORRECT_NUMBER,
  SEQ_OUT_OF, SEQ_TOTAL_NUMBER, SEQ_CORRECT_WORD, SEQ_DONE
};
ScoreSequence scoreSequence          = SEQ_NONE;
bool          playingScore           = false;
bool          returnToQuizAfterScore = false;

// =====================================================
// RUNTIME STATE
// =====================================================
int    correctAnswersCount    = 0;
int    wrongAnswersCount      = 0;
int    totalQuestionsAnswered = 0;
int8_t currentQuestionIndex   = -1;
bool   lastAnswerWasCorrect   = false;

unsigned long lastAnswerTime  = 0;
const unsigned long ANSWER_COOLDOWN      = 300;
const unsigned long AUTO_NEXT_DELAY      = 1500;
const unsigned long FEEDBACK_DELAY       = 100;
const unsigned long DOUBLE_PRESS_WINDOW  = 400;
bool playingInstructions = false;
bool playedWelcome       = false;
bool playedModeAnnounce  = false;
bool audioFileValid      = false;
int8_t currentTrackIndex = -1;
float  volumeGain        = 2.5f;

SemaphoreHandle_t sdMutex;

#define I2S_BUFFER_SIZE 1024
int16_t  i2sBuffer[I2S_BUFFER_SIZE];
size_t   i2sBufferPos    = 0;
size_t   i2sBufferLength = 0;
File     audioFile;
unsigned long lastAudioTime = 0;
const unsigned long AUDIO_INTERVAL = 5;

// =====================================================
// BUTTON → PHYSICAL STATE INDEX MAP
// =====================================================
const int8_t buttonToPhysical[40] = {
   0,  1,  2,  3,  4,  5,  6,  7,
   8,  9, 10, 11, 12, 13, 14, 15,
  20, 21, 22, 23, 16, 17, 18, 19,
  28, 29, 30, 31, 24, 25, 26, 27,
  32, 33, 34, 35, -1, -1, -1, -1
};

// RFID globals
String        lastCardUID  = "";
unsigned long lastCardTime  = 0;
unsigned long lastRFIDCheck = 0;
const unsigned long RFID_CHECK_INTERVAL = 100;

struct CardMap {
  const char* name;
  int          stateIndex;   
};
const CardMap cardList[] = {
  {"UTTARPRADESH",     0}, {"RAJASTHAN",         1}, {"BIHAR",             2},
  {"SIKKIM",           3}, {"HIMACHALPRADESH",   4}, {"PUNJAB",            5},
  {"UTTARAKHAND",      6}, {"HARYANA",           7}, {"TRIPURA",           8},
  {"MEGHALAYA",        9}, {"ASSAM",            10}, {"WESTBENGAL",       11},
  {"ARUNACHALPRADESH",12}, {"NAGALAND",         13}, {"MANIPUR",          14},
  {"MIZORAM",          15}, {"JHARKHAND",        16}, {"CHHATTISGARH",    17},
  {"MADHYAPRADESH",    18}, {"GUJARAT",          19}, {"ODISHA",          20},
  {"MAHARASHTRA",      21}, {"TELANGANA",        22}, {"ANDHRAPRADESH",   23},
  {"GOA",              24}, {"KARNATAKA",        25}, {"KERALA",           26},
  {"TAMILNADU",        27}, {"LADAKH",           28}, {"JAMMUKASHMIR",     29},
  {"CHANDIGARH",       30}, {"DELHI",            31}, {"LAKSHADWEEP",      32},
  {"ANI",              33}, {"DNHDD",            34}, {"PUDUCHERRY",       35}
};

// =====================================================
// 74HC595 write (5-chip chain, 40 bits)
// =====================================================
void write595_all(uint64_t value) {
  digitalWrite(LATCH, LOW);
  for (int i = NUM_595_CHIPS - 1; i >= 0; --i) {
    uint8_t b = (value >> (i * 8)) & 0xFF;
    shiftOut(DATA, CLOCK, MSBFIRST, b);
  }
  digitalWrite(LATCH, HIGH);
  delayMicroseconds(5);
}

// =====================================================
// Forward Declarations
// =====================================================
void playQuestion();
void nextQuestion();
void announceScore();
void continueScoreAnnouncement();
void processAnswer(int8_t physicalSwitch);
void exitQuizMode();
void resetBoard();
String getUidString(MFRC522::Uid &u);
String readBlock4String();
int   findCardIndex(String cardData);
void  toggleRFIDMode();
void  enableRFID();
void  disableRFID();
uint64_t read165_all();
void vTaskReadButtons(void *pvParameters);
void vTaskRFID(void *pvParameters);
bool openAndStartAudio(const char* filename, bool isFeedback = false, bool isQuestion = false);
bool openAndStartAudio(const String& filename, bool isFeedback = false, bool isQuestion = false);
bool loadBluetoothName();
void selectLanguage(SystemLanguage lang);
void deleteSpeakerConfigAndRestart();
void runSpeakerSetupPortal();
void processPendingAction();
#ifdef DEBUG_165
void vTaskDebug165(void *pvParameters);
#endif

// =====================================================
// LED helpers
// =====================================================
void updateAnswerLEDsFromLastResult() {
  digitalWrite(CORRECT_LED, lastAnswerWasCorrect ? HIGH : LOW);
  digitalWrite(WRONG_LED,   lastAnswerWasCorrect ? LOW  : HIGH);
}
void resetAnswerLEDs() {
  digitalWrite(CORRECT_LED, LOW);
  digitalWrite(WRONG_LED, LOW);
  lastAnswerWasCorrect = false;
}
void setWelcomeLights(bool on) {
  ledState = on ? LED_ALL_ON : LED_ALL_OFF;
  write595_all(ledState);
  digitalWrite(CORRECT_LED, on ? HIGH : LOW);
  digitalWrite(WRONG_LED,   on ? HIGH : LOW);
}
void flashResetLEDs() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(CORRECT_LED, HIGH);
    digitalWrite(WRONG_LED,   HIGH);
    delay(150);
    digitalWrite(CORRECT_LED, LOW);
    digitalWrite(WRONG_LED,   LOW);
    delay(100);
  }
}

// =====================================================
// SD helpers (mutex-safe)
// =====================================================
File safeOpenFile(const char* filename, const char* mode = FILE_READ) {
  File file;
  if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
    file = SD.open(filename, mode);
    xSemaphoreGive(sdMutex);
  }
  return file;
}
void safeCloseFile(File &file) {
  if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
    if (file) file.close();
    xSemaphoreGive(sdMutex);
  }
}
size_t safeReadFile(File &file, uint8_t* buffer, size_t size) {
  size_t bytesRead = 0;
  if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
    if (file) bytesRead = file.read(buffer, size);
    xSemaphoreGive(sdMutex);
  }
  return bytesRead;
}
bool safeSeekFile(File &file, uint32_t pos) {
  bool res = false;
  if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
    if (file) res = file.seek(pos);
    xSemaphoreGive(sdMutex);
  }
  return res;
}

// =====================================================
// Config storage
// =====================================================
bool readTextFile(const char* filename, String &out) {
  out = "";
  bool ok = false;
  if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
    File file = SD.open(filename, FILE_READ);
    if (file) {
      while (file.available()) out += (char)file.read();
      file.close();
      out.trim();
      ok = out.length() > 0;
    }
    xSemaphoreGive(sdMutex);
  }
  return ok;
}
bool writeTextFile(const char* filename, const String& value) {
  bool ok = false;
  if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
    if (SD.exists(filename)) SD.remove(filename);
    File file = SD.open(filename, FILE_WRITE);
    if (file) {
      file.print(value);
      file.println();
      file.close();
      ok = true;
    }
    xSemaphoreGive(sdMutex);
  }
  return ok;
}
bool deleteTextFile(const char* filename) {
  bool removed = false;
  if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
    if (SD.exists(filename)) removed = SD.remove(filename);
    xSemaphoreGive(sdMutex);
  }
  return removed;
}

bool loadBluetoothName() {
  String savedName;
  if (readTextFile(SPEAKER_CONFIG_FILE, savedName)) {
    savedName.toCharArray(btSpeakerName, sizeof(btSpeakerName));
    btSpeakerName[sizeof(btSpeakerName) - 1] = '\0';
    Serial.print("Bluetooth speaker from SD: ");
    Serial.println(btSpeakerName);
    return true;
  }
  strncpy(btSpeakerName, DEFAULT_BT_NAME, sizeof(btSpeakerName) - 1);
  btSpeakerName[sizeof(btSpeakerName) - 1] = '\0';
  Serial.println("No speaker.txt found; setup portal required");
  return false;
}
bool saveBluetoothName(const String& speakerName) {
  String cleaned = speakerName;
  cleaned.trim();
  if (cleaned.length() == 0 || cleaned.length() >= sizeof(btSpeakerName)) return false;
  return writeTextFile(SPEAKER_CONFIG_FILE, cleaned);
}

// =====================================================
// Language helpers
// =====================================================
void playLanguageAnnouncement() {
  safeCloseFile(audioFile);
  streamPaused = localPaused = true;
  if (quizMode && currentQuestionIndex >= 0) quizState = QUIZ_IDLE;
  String path = getLocalizedAudio(languageChangedAudioPaths);
  bool started = openAndStartAudio(path, true);
  if (!started && quizMode && currentQuestionIndex >= 0) {
    delay(100);
    playQuestion();
  }
}

void selectLanguage(SystemLanguage lang) {
  currentLanguage = lang;
  Serial.print("Language set to: ");
  Serial.println(languageNames[currentLanguage]);
  playLanguageAnnouncement();
}

// =====================================================
// Setup portal (captive-portal for speaker name)
// =====================================================
String setupPortalPage(const String& message) {
  String page = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>Bharat Setup</title><style>";
  page += "body{font-family:Arial,sans-serif;margin:0;background:#f5f7fb;color:#18202f}";
  page += "main{max-width:520px;margin:40px auto;padding:24px}";
  page += "label,input,button{display:block;width:100%;box-sizing:border-box;font-size:18px}";
  page += "input{margin:10px 0 16px;padding:14px;border:1px solid #a8b0bf;border-radius:6px}";
  page += "button{padding:14px;border:0;border-radius:6px;background:#0b6bcb;color:white}";
  page += "p{line-height:1.45}.msg{padding:12px;background:#fff3cd;border-radius:6px}";
  page += "</style></head><body><main>";
  page += "<h1>Bharat Speaker Setup</h1>";
  page += "<p>Enter the exact Bluetooth speaker name once. It will be saved on the SD card as speaker.txt.</p>";
  if (message.length()) { page += "<p class='msg'>"; page += message; page += "</p>"; }
  page += "<form method='POST' action='/save'>";
  page += "<label for='speaker'>Bluetooth speaker name</label>";
  page += "<input id='speaker' name='speaker' maxlength='63' autofocus required>";
  page += "<button type='submit'>Save speaker</button>";
  page += "</form></main></body></html>";
  return page;
}
void handleSetupRoot()    { setupServer.send(200, "text/html", setupPortalPage("")); }
void handleSetupSave() {
  String n = setupServer.arg("speaker"); n.trim();
  if (!saveBluetoothName(n)) {
    setupServer.send(400, "text/html", setupPortalPage("Please enter a speaker name under 64 characters."));
    return;
  }
  setupPortalSaved = true;
  setupServer.send(200, "text/html", setupPortalPage("Saved. The board will restart."));
}
void handleSetupNotFound() {
  setupServer.sendHeader("Location", "http://192.168.4.1/", true);
  setupServer.send(302, "text/plain", "");
}
void runSpeakerSetupPortal() {
  setupPortalSaved = false;
  WiFi.mode(WIFI_AP);
  IPAddress apIP(192,168,4,1), subnet(255,255,255,0);
  WiFi.softAPConfig(apIP, apIP, subnet);
  WiFi.softAP(SETUP_AP_SSID);
  setupDns.start(DNS_PORT, "*", apIP);
  setupServer.on("/",       HTTP_GET,  handleSetupRoot);
  setupServer.on("/save",   HTTP_POST, handleSetupSave);
  setupServer.onNotFound(handleSetupNotFound);
  setupServer.begin();
  Serial.println("Open WiFi AP started: Bharat-Setup");
  while (!setupPortalSaved) {
    setupDns.processNextRequest();
    setupServer.handleClient();
    delay(10);
  }
  delay(1500);
  setupServer.stop(); setupDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  ESP.restart();
}
void deleteSpeakerConfigAndRestart() {
  safeCloseFile(audioFile);
  streamPaused = localPaused = true;
  deleteTextFile(SPEAKER_CONFIG_FILE);
  Serial.println("speaker.txt deleted. Restarting into setup portal...");
  delay(1000);
  ESP.restart();
}

// =====================================================
// 74HC165 read (5-chip chain, 40 bits)
// =====================================================
uint64_t read165_all() {
  digitalWrite(PL, LOW);  delayMicroseconds(10);
  digitalWrite(PL, HIGH); digitalWrite(CP, LOW);
  uint64_t value = 0;
  for (int i = 0; i < 5; ++i) {
    uint8_t b = shiftIn(Q7, CP, MSBFIRST);
    value = (value << 8) | b;
  }
  delayMicroseconds(2);
  value &= 0x000000FFFFFFFFFFULL;
#if PRESSED_ACTIVE_LOW
  value = (~value) & 0x000000FFFFFFFFFFULL;
#endif
  return value;
}

#ifdef DEBUG_165
void vTaskDebug165(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    uint64_t raw = read165_all();
    Serial.printf("RAW: 0x%010llX\n", (unsigned long long)raw);
    for (int b = 0; b < 40; ++b) {
      Serial.printf("%02d:%d ", b, (int)((raw >> b) & 1));
      if ((b % 8) == 7) Serial.println();
    }
    Serial.println("\n----");
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}
#endif

// =====================================================
// Button debounce task
// =====================================================
void vTaskReadButtons(void *pvParameters) {
  (void)pvParameters;
  const TickType_t tickMs = pdMS_TO_TICKS(20);
  uint64_t lastRaw     = read165_all();
  uint8_t  counter[40] = {0};
  uint64_t stableState = lastRaw;
  uint64_t lastStableState = stableState;
  vTaskDelay(pdMS_TO_TICKS(50));
  for (;;) {
    uint64_t raw = read165_all();
    for (int i = 0; i < 40; i++) {
      uint8_t bit = (raw >> i) & 1;
      if (bit != ((lastRaw >> i) & 1)) { counter[i] = 0; }
      else if (++counter[i] == 3) {
        if (((stableState >> i) & 1) != bit) stableState ^= (1ULL << i);
      }
    }
    lastRaw = raw;
    if (stableState != lastStableState) {
      xQueueOverwrite(buttonQueue, &stableState);
      lastStableState = stableState;
    }
    vTaskDelay(tickMs);
  }
}

// =====================================================
// Audio data reader / WAV header / I2S init
// =====================================================
size_t readAudioData(File &file, int16_t* buffer, size_t samples) {
  size_t bytesPerSample = (bitsPerSample / 8) * numChannels;
  size_t bytesToRead    = samples * bytesPerSample;
  
  // Guard against reading more than our large global buffer holds
  if (bytesToRead > sizeof(wavReadBuffer)) bytesToRead = sizeof(wavReadBuffer);
  
  size_t bytesRead   = safeReadFile(file, wavReadBuffer, bytesToRead);
  size_t samplesRead = 0;
  
  if (bytesRead > 0 && bitsPerSample == 16) {
    for (size_t i = 0; i + 1 < bytesRead && samplesRead < samples; i += bytesPerSample) {
      int16_t sample   = wavReadBuffer[i] | (wavReadBuffer[i + 1] << 8);
      float amplified  = sample * volumeGain;
      amplified        = constrain(amplified, -32768.0f, 32767.0f);
      buffer[samplesRead++] = (int16_t)amplified;
    }
  }
  return samplesRead;
}

bool readWavHeader(File &file) {
  if (!file) return false;
  safeSeekFile(file, 0);
  char riff[5] = {0};
  if (safeReadFile(file, (uint8_t*)riff, 4) != 4) return false;
  if (strcmp(riff, "RIFF") != 0) return false;
  uint32_t chunkSize;
  safeReadFile(file, (uint8_t*)&chunkSize, 4);
  char wave[5] = {0};
  if (safeReadFile(file, (uint8_t*)wave, 4) != 4) return false;
  if (strcmp(wave, "WAVE") != 0) return false;
  while (file.available()) {
    char     chunkId[5] = {0};
    uint32_t cSize;
    if (safeReadFile(file, (uint8_t*)chunkId, 4) != 4) break;
    if (safeReadFile(file, (uint8_t*)&cSize,  4) != 4) break;
    if (strcmp(chunkId, "fmt ") == 0) {
      uint16_t format = 0, channels = 0, bits = 0;
      uint32_t rate   = 0;
      safeReadFile(file, (uint8_t*)&format,   2);
      safeReadFile(file, (uint8_t*)&channels, 2);
      safeReadFile(file, (uint8_t*)&rate,     4);
      safeSeekFile(file, file.position() + 6);
      safeReadFile(file, (uint8_t*)&bits, 2);
      numChannels   = channels;
      sampleRate    = rate;
      bitsPerSample = bits;
      if (cSize > 16) safeSeekFile(file, file.position() + (cSize - 16));
    } else if (strcmp(chunkId, "data") == 0) {
      dataSize = cSize;
      return true;
    } else {
      safeSeekFile(file, file.position() + cSize);
    }
  }
  return false;
}

void initI2S() {
  i2s_driver_uninstall(I2S_NUM_0);
  i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = 44100,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    // CRITICAL: Decreased DMA buffer footprint to free up Heap for the SBC Encoder
    .dma_buf_count        = 4,
    .dma_buf_len          = 512,
    .use_apll             = true,
    .tx_desc_auto_clear   = true,
    .fixed_mclk           = 0
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num   = I2S_BCLK,
    .ws_io_num    = I2S_LRCK,
    .data_out_num = I2S_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_set_sample_rates(I2S_NUM_0, 44100);
  Serial.println("I2S initialized");
}

// =====================================================
// Open and start audio file implementations
// =====================================================
bool openAndStartAudio(const char* filename, bool isFeedback, bool isQuestion) {
  // CRITICAL THREAD BARRIER: Block the high-priority Bluetooth task from 
  // colliding with the SD card file swapping
  streamPaused = true;
  localPaused  = true;
  delay(5); 

  uint32_t t = millis();
  const int MAX_TRIES = 2;
  for (int attempt = 1; attempt <= MAX_TRIES; ++attempt) {
    if (!SD.exists(filename)) {
      Serial.print("Missing: ");
      Serial.println(filename);
      return false;
    }
    
    safeCloseFile(audioFile);
    audioFile = safeOpenFile(filename);
    
    if (!audioFile) { delay(50); continue; }
    audioFileValid = readWavHeader(audioFile);
    if (!audioFileValid) { safeCloseFile(audioFile); delay(50); continue; }
    
    if (bluetoothMode) {
      i2sBufferLength = readAudioData(audioFile, i2sBuffer, I2S_BUFFER_SIZE);
      i2sBufferPos    = 0;
      streamPaused    = false;
      localPaused     = true;
    } else {
      i2sBufferLength = 0;
      i2sBufferPos    = 0;
      localPaused     = false;
      streamPaused    = true;
    }
    Serial.print("Playing: "); Serial.println(filename);
    Serial.printf("Audio startup took %lu ms\n", millis() - t);
    return true;
  }
  return false;
}

bool openAndStartAudio(const String& filename, bool isFeedback, bool isQuestion) {
  return openAndStartAudio(filename.c_str(), isFeedback, isQuestion);
}

// =====================================================
// RESET BOARD  — clears all state, plays reset clip
// =====================================================
void resetBoard() {
  Serial.println("\n=== BOARD RESET ===");
  safeCloseFile(audioFile);
  streamPaused = localPaused = true;
  quizMode             = false;
  quizState            = QUIZ_IDLE;
  currentQuestionIndex = -1;
  currentTrackIndex    = -1;
  playingScore         = false;
  scoreSequence        = SEQ_NONE;
  returnToQuizAfterScore = false;
  playingInstructions  = false;
  pendingAction        = PENDING_NONE;
  correctAnswersCount  = 0;
  wrongAnswersCount    = 0;
  totalQuestionsAnswered = 0;
  memset(questionsAsked, 0, sizeof(questionsAsked));
  totalQuestionsAsked  = 0;
  ledState = LED_ALL_OFF;
  write595_all(ledState);
  flashResetLEDs();
  resetAnswerLEDs();
  openAndStartAudio(getLocalizedAudio(resetAudioPaths), true);
}

// =====================================================
// Quiz mode
// =====================================================
void enterQuizMode() {
  Serial.println("\n=== QUIZ MODE ===");
  safeCloseFile(audioFile);
  streamPaused = localPaused = true;
  quizMode  = true;
  memset(questionsAsked, 0, sizeof(questionsAsked));
  totalQuestionsAsked    = 0;
  correctAnswersCount    = 0;
  wrongAnswersCount      = 0;
  totalQuestionsAnswered = 0;
  playingScore           = false;
  scoreSequence          = SEQ_NONE;
  returnToQuizAfterScore = false;
  pendingAction          = PENDING_NONE;
  resetAnswerLEDs();
  ledState = LED_ALL_OFF;
  write595_all(ledState);
  currentQuestionIndex = findNextSequentialQuestion();
  quizState = QUIZ_IDLE;
  openAndStartAudio(getModeAnnouncement(), true);
}

void playQuestion() {
  if (!quizMode || currentQuestionIndex < 0) return;
  if (!questionsAsked[currentQuestionIndex]) {
    questionsAsked[currentQuestionIndex] = true;
    totalQuestionsAsked++;
  }
  resetAnswerLEDs();
  ledState = LED_ALL_OFF;
  write595_all(ledState);
  safeCloseFile(audioFile);
  String qfile = getQuizQuestionPath(currentQuestionIndex);
  Serial.print("\nQ"); Serial.print(totalQuestionsAnswered + 1);
  Serial.print(" [idx="); Serial.print(currentQuestionIndex);
  Serial.println("]");
  if (openAndStartAudio(qfile, false, true)) {
    quizState = QUIZ_PLAYING_QUESTION;
  } else {
    Serial.println("Question file missing, skipping...");
    currentQuestionIndex = findNextSequentialQuestion();
    if (currentQuestionIndex >= 0) playQuestion();
  }
}

void nextQuestion() {
  if (!quizMode) return;
  safeCloseFile(audioFile);
  streamPaused = localPaused = true;
  resetAnswerLEDs();
  ledState = LED_ALL_OFF;
  write595_all(ledState);
  currentQuestionIndex = findNextSequentialQuestion();
  if (currentQuestionIndex == -1) {
    memset(questionsAsked, 0, sizeof(questionsAsked));
    totalQuestionsAsked = 0;
    Serial.println("\n=== QUESTIONS RESET ===");
    currentQuestionIndex = findNextSequentialQuestion();
  }
  if (currentQuestionIndex >= 0) playQuestion();
}

void processAnswer(int8_t physicalSwitch) {
  if (!quizMode || quizState != QUIZ_WAITING_FOR_ANSWER
      || physicalSwitch < 0 || physicalSwitch >= 36) return;
  if (playingInstructions || playingScore) return;
  if (millis() - lastAnswerTime < ANSWER_COOLDOWN) return;
  lastAnswerTime = millis();

  lastAnswerWasCorrect = isCorrectAnswer(physicalSwitch, currentQuestionIndex);
  totalQuestionsAnswered++;
  if (lastAnswerWasCorrect) {
    correctAnswersCount++;
  } else {
    wrongAnswersCount++;
  }
  Serial.print(lastAnswerWasCorrect ? "CORRECT" : "WRONG");
  Serial.print(" | Score: "); Serial.print(correctAnswersCount);
  Serial.print("/"); Serial.println(totalQuestionsAnswered);
  streamPaused = localPaused = true;
  safeCloseFile(audioFile);

  ledState = LED_ALL_OFF;
  if (lastAnswerWasCorrect) {
    int ledToLight = switchToLED[physicalSwitch];
    if (ledToLight >= 0) ledState &= ~(1ULL << ledToLight);
  }
  write595_all(ledState);
  updateAnswerLEDsFromLastResult();

  quizState = QUIZ_PLAYING_FEEDBACK;
  delay(FEEDBACK_DELAY);
  openAndStartAudio(getLocalizedAudio(lastAnswerWasCorrect ? correctAudioPaths : wrongAudioPaths), true);
}

void exitQuizMode() {
  Serial.println("\n=== QUIZ SESSION SUMMARY ===");
  Serial.print("Correct: "); Serial.print(correctAnswersCount);
  Serial.print(" / "); Serial.println(totalQuestionsAnswered);

  if (totalQuestionsAnswered > 0) {
    announceScore();
    unsigned long scoreWaitStart = millis();
    while (playingScore && (millis() - scoreWaitStart < 30000)) {
      // Loop thread owns processing of actions, do not duplicate execute here
      delay(20);
    }
  }

  safeCloseFile(audioFile);
  streamPaused = localPaused = true;
  quizMode             = false;
  currentQuestionIndex = -1;
  currentTrackIndex    = -1;
  playingScore         = false;
  scoreSequence        = SEQ_NONE;
  returnToQuizAfterScore = false;
  pendingAction        = PENDING_NONE;
  resetAnswerLEDs();
  ledState = LED_ALL_OFF;
  write595_all(ledState);
  playingInstructions  = false;
  openAndStartAudio(getModeAnnouncement(), true);
}

// =====================================================
// Score announcement
// =====================================================
void announceScore() {
  if (totalQuestionsAnswered == 0) return;
  Serial.print("Score: "); Serial.print(correctAnswersCount);
  Serial.print("/"); Serial.println(totalQuestionsAnswered);
  returnToQuizAfterScore = quizMode;
  scoreSequence = SEQ_SCORE_INTRO;
  playingScore  = true;
  continueScoreAnnouncement();
}

void continueScoreAnnouncement() {
  if (!playingScore || scoreSequence == SEQ_NONE) return;
  safeCloseFile(audioFile);
  streamPaused = localPaused = true;
  switch (scoreSequence) {
    case SEQ_SCORE_INTRO: {
      if (openAndStartAudio(getLocalizedAudio(scoreIntroAudioPaths)))
        scoreSequence = SEQ_CORRECT_NUMBER;
      else { scoreSequence = SEQ_CORRECT_NUMBER; continueScoreAnnouncement(); }
      break;
    }
    case SEQ_CORRECT_NUMBER: {
      String f = getNumberAudioPath(correctAnswersCount);
      if (f.length() && openAndStartAudio(f)) scoreSequence = SEQ_OUT_OF;
      else { scoreSequence = SEQ_DONE; continueScoreAnnouncement(); }
      break;
    }
    case SEQ_OUT_OF: {
      if (openAndStartAudio(getLocalizedAudio(outOfAudioPaths)))
        scoreSequence = SEQ_TOTAL_NUMBER;
      else { scoreSequence = SEQ_TOTAL_NUMBER; continueScoreAnnouncement(); }
      break;
    }
    case SEQ_TOTAL_NUMBER: {
      String f = getNumberAudioPath(totalQuestionsAnswered);
      if (f.length() && openAndStartAudio(f)) scoreSequence = SEQ_CORRECT_WORD;
      else { scoreSequence = SEQ_CORRECT_WORD; continueScoreAnnouncement(); }
      break;
    }
    case SEQ_CORRECT_WORD: {
      if (openAndStartAudio(String(correctWordAudio))) scoreSequence = SEQ_DONE;
      else { scoreSequence = SEQ_DONE; continueScoreAnnouncement(); }
      break;
    }
    case SEQ_DONE:
      playingScore  = false;
      scoreSequence = SEQ_NONE;
      Serial.println("Score done");
      if (returnToQuizAfterScore && quizMode) {
        returnToQuizAfterScore = false;
      }
      break;
    default:
      playingScore  = false;
      scoreSequence = SEQ_NONE;
      break;
  }
}

// =====================================================
// Instructions
// =====================================================
void playInstructions() {
  bool    wasInQuiz  = quizMode;
  int8_t  savedIdx   = currentQuestionIndex;
  safeCloseFile(audioFile);
  streamPaused = localPaused = true;
  resetAnswerLEDs();
  ledState = LED_ALL_ON;
  write595_all(ledState);
  playingInstructions = true;
  if (!openAndStartAudio(getLocalizedAudio(instructionsAudioPaths))) {
    playingInstructions = false;
    ledState = LED_ALL_OFF;
    write595_all(ledState);
    if (wasInQuiz && savedIdx >= 0) { currentQuestionIndex = savedIdx; playQuestion(); }
  }
}

// =====================================================
// RFID
// =====================================================
void enableRFID() {
  if (!rfidInitialized) {
    digitalWrite(RFID_RST, LOW);  delay(50);
    digitalWrite(RFID_RST, HIGH); delay(50);
    mfrc.PCD_Init(); delay(4);
    byte v = mfrc.PCD_ReadRegister(mfrc.VersionReg);
    Serial.print("MFRC522 v: 0x"); Serial.println(v, HEX);
    rfidInitialized = true;
  }
}
void disableRFID() {
  if (rfidInitialized) {
    mfrc.PCD_SoftPowerDown(); delay(10);
    digitalWrite(RFID_RST, LOW); delay(50);
    rfidInitialized = false;
  }
}
void toggleRFIDMode() {
  rfidMode = !rfidMode;
  Serial.print("RFID Mode: "); Serial.println(rfidMode ? "ON" : "OFF");
  rfidMode ? enableRFID() : disableRFID();
  ledState = LED_ALL_ON;  write595_all(ledState);
  delay(150);
  ledState = LED_ALL_OFF; write595_all(ledState);
  openAndStartAudio(getModeAnnouncement(), true);
}

String getUidString(MFRC522::Uid &u) {
  String s = "";
  for (byte i = 0; i < u.size; i++) {
    if (u.uidByte[i] < 0x10) s += "0";
    s += String(u.uidByte[i], HEX);
  }
  s.toLowerCase();
  return s;
}

String readBlock4String() {
  MFRC522::MIFARE_Key key;
  memset(key.keyByte, 0xFF, MFRC522::MF_KEY_SIZE);
  byte block = 4, buffer[18], bufferSize = sizeof(buffer);
  MFRC522::StatusCode status = mfrc.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Auth failed: "); Serial.println(mfrc.GetStatusCodeName(status));
    return "";
  }
  status = mfrc.MIFARE_Read(block, buffer, &bufferSize);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Read failed: "); Serial.println(mfrc.GetStatusCodeName(status));
    return "";
  }
  String result = "";
  for (int i = 0; i < 16; i++) {
    char c = (char)buffer[i];
    if (c == 0x00) break;
    result += (char)toupper(c);
  }
  result.trim();
  return result;
}

int findCardIndex(String cardData) {
  cardData.trim();
  const size_t cardCount = sizeof(cardList) / sizeof(cardList[0]);
  for (size_t i = 0; i < cardCount; i++) {
    if (cardData.equalsIgnoreCase(cardList[i].name)) return (int)i;
  }
  return -1;
}

void vTaskRFID(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    if (rfidMode && rfidInitialized) {
      unsigned long now = millis();
      if (now - lastRFIDCheck >= RFID_CHECK_INTERVAL) {
        lastRFIDCheck = now;
        if (mfrc.PICC_IsNewCardPresent() && mfrc.PICC_ReadCardSerial()) {
          String cardData = readBlock4String();
          String uid      = getUidString(mfrc.uid);
          if (uid == lastCardUID && now - lastCardTime < ANSWER_COOLDOWN) {
            mfrc.PICC_HaltA();
            mfrc.PCD_StopCrypto1();
          } else {
            lastCardUID  = uid;
            lastCardTime = now;
            Serial.print("RFID: "); Serial.println(cardData);
            int listIdx = findCardIndex(cardData);
            if (listIdx >= 0) {
              int8_t physical = (int8_t)cardList[listIdx].stateIndex;
              if (quizMode) {
                if (quizState == QUIZ_WAITING_FOR_ANSWER && !playingScore)
                  processAnswer(physical);
              } else {
                if (!playingScore) {
                  ledState = LED_ALL_OFF;
                  int led  = switchToLED[physical];
                  if (led >= 0) ledState &= ~(1ULL << led);
                  currentTrackIndex = physical;
                  safeCloseFile(audioFile);
                  openAndStartAudio(getSwitchAudioPath(physical));
                  write595_all(ledState);
                }
              }
            }
            mfrc.PICC_HaltA();
            mfrc.PCD_StopCrypto1();
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =====================================================
// Deferred Action Processing Loop Thread
// =====================================================
void processPendingAction() {
  PendingAction action = pendingAction;
  if (action == PENDING_NONE) return;
  switch (action) {
    case PENDING_PLAY_QUESTION_AFTER_INSTRUCTIONS:
      pendingAction = PENDING_NONE;
      if (quizMode && currentQuestionIndex >= 0) playQuestion();
      break;

    case PENDING_CONTINUE_SCORE:
      pendingAction = PENDING_NONE;
      continueScoreAnnouncement();
      break;
    case PENDING_PLAY_QUESTION_IDLE:
      pendingAction = PENDING_NONE;
      if (currentQuestionIndex >= 0) playQuestion();
      break;
    case PENDING_NEXT_QUESTION_DELAYED:
      if (millis() - pendingActionAt < AUTO_NEXT_DELAY) return;
      pendingAction = PENDING_NONE;
      nextQuestion();
      break;
    case PENDING_PLAY_CORRECT_ANNOUNCEMENT: {
      pendingAction = PENDING_NONE;
      int8_t cs = pendingCorrectIdx;
      pendingCorrectIdx = -1;
      if (cs < 0) { quizState = QUIZ_QUESTION_COMPLETE; break; }
      ledState = LED_ALL_OFF;
      int led  = switchToLED[cs];
      if (led >= 0) ledState &= ~(1ULL << led);
      write595_all(ledState);
      if (openAndStartAudio(getCorrectAnnouncementPath(cs), true))
        quizState = QUIZ_PLAYING_CORRECT_ANSWER;
      else
        quizState = QUIZ_QUESTION_COMPLETE;
      break;
    }

    case PENDING_NEXT_QUESTION_AFTER_CORRECT_ANNOUNCEMENT:
      if (millis() - pendingActionAt < AUTO_NEXT_DELAY) return;
      pendingAction = PENDING_NONE;
      nextQuestion();
      break;

    case PENDING_MODE_ANNOUNCEMENT:
      pendingAction = PENDING_NONE;
      playedModeAnnounce = true;
      setWelcomeLights(false);
      openAndStartAudio(getModeAnnouncement(), true);
      break;

    default:
      pendingAction = PENDING_NONE;
      break;
  }
}

// =====================================================
// Bluetooth Data Pump Callback
// =====================================================
int32_t get_sound_data(Frame *frame, int32_t frame_count) {
  if (streamPaused || !audioFile || !audioFileValid || !bluetoothMode) {
    memset(frame, 0, frame_count * sizeof(Frame));
    return frame_count;
  }
  for (int i = 0; i < frame_count; i++) {
    if (i2sBufferPos >= i2sBufferLength) {
      i2sBufferLength = readAudioData(audioFile, i2sBuffer, I2S_BUFFER_SIZE);
      i2sBufferPos    = 0;
      if (i2sBufferLength == 0) {
        memset(&frame[i], 0, (frame_count - i) * sizeof(Frame));
        streamPaused = true;   

        if (playingInstructions) {
          playingInstructions = false;
          if (quizMode && currentQuestionIndex >= 0)
            pendingAction = PENDING_PLAY_QUESTION_AFTER_INSTRUCTIONS;
        } else if (playingScore) {
          pendingAction = PENDING_CONTINUE_SCORE;
        } else if (quizMode) {
          if (quizState == QUIZ_IDLE) {
            if (currentQuestionIndex >= 0) pendingAction = PENDING_PLAY_QUESTION_IDLE;
          } else if (quizState == QUIZ_PLAYING_QUESTION) {
            quizState = QUIZ_WAITING_FOR_ANSWER;
          } else if (quizState == QUIZ_PLAYING_FEEDBACK) {
            if (lastAnswerWasCorrect) {
              quizState = QUIZ_QUESTION_COMPLETE;
              pendingActionAt = millis();
              pendingAction   = PENDING_NEXT_QUESTION_DELAYED;
            } else {
              pendingCorrectIdx = getFirstCorrectAnswer(currentQuestionIndex);
              pendingAction     = PENDING_PLAY_CORRECT_ANNOUNCEMENT;
            }
          } else if (quizState == QUIZ_PLAYING_CORRECT_ANSWER) {
            quizState = QUIZ_QUESTION_COMPLETE;
            pendingActionAt = millis();
            pendingAction   = PENDING_NEXT_QUESTION_AFTER_CORRECT_ANNOUNCEMENT;
          }
        } else {
          safeCloseFile(audioFile);
          if (playedWelcome && !playedModeAnnounce && currentTrackIndex == -1
              && !playingInstructions && !playingScore) {
            pendingAction = PENDING_MODE_ANNOUNCEMENT;
          }
        }
        return frame_count;
      }
    }
    int16_t sample = i2sBuffer[i2sBufferPos++];
    frame[i].channel1 = frame[i].channel2 = sample;
  }
  return frame_count;
}

// =====================================================
// Local I2S Playback
// =====================================================
void playLocalAudio() {
  if (localPaused || !audioFile || !audioFileValid || bluetoothMode) return;
  if (millis() - lastAudioTime < AUDIO_INTERVAL) return;
  lastAudioTime = millis();
  if (i2sBufferPos >= i2sBufferLength) {
    i2sBufferLength = readAudioData(audioFile, i2sBuffer, I2S_BUFFER_SIZE);
    i2sBufferPos    = 0;
    if (i2sBufferLength == 0) {
      if (playingInstructions) {
        playingInstructions = false;
        ledState = LED_ALL_OFF;
        write595_all(ledState);
        if (quizMode && currentQuestionIndex >= 0) { delay(300); playQuestion(); }
      } else if (playingScore) {
        continueScoreAnnouncement();
      } else if (quizMode) {
        if (quizState == QUIZ_IDLE) {
          if (currentQuestionIndex >= 0) playQuestion();
        } else if (quizState == QUIZ_PLAYING_QUESTION) {
          quizState = QUIZ_WAITING_FOR_ANSWER;
        } else if (quizState == QUIZ_PLAYING_FEEDBACK) {
          if (lastAnswerWasCorrect) {
            quizState   = QUIZ_QUESTION_COMPLETE;
            localPaused = true;
            safeCloseFile(audioFile);
            delay(AUTO_NEXT_DELAY);
            nextQuestion();
            return;
          } else {
            int cs  = getFirstCorrectAnswer(currentQuestionIndex);
            ledState = LED_ALL_OFF;
            int led  = switchToLED[cs];
            if (led >= 0) ledState &= ~(1ULL << led);
            write595_all(ledState);
            if (openAndStartAudio(getCorrectAnnouncementPath(cs), true))
              quizState = QUIZ_PLAYING_CORRECT_ANSWER;
            else
              quizState = QUIZ_QUESTION_COMPLETE;
          }
        } else if (quizState == QUIZ_PLAYING_CORRECT_ANSWER) {
          quizState   = QUIZ_QUESTION_COMPLETE;
          localPaused = true;
          safeCloseFile(audioFile);
          delay(AUTO_NEXT_DELAY);
          nextQuestion();
          return;
        } else if (quizState == QUIZ_QUESTION_COMPLETE) {
          nextQuestion();
        }
        localPaused = true;
        safeCloseFile(audioFile);
      } else {
        localPaused = true;
        safeCloseFile(audioFile);
      }
      return;
    }
  }
  size_t toWrite = (i2sBufferLength - i2sBufferPos) * 2;
  size_t written;
  i2s_write(I2S_NUM_0, (char*)&i2sBuffer[i2sBufferPos], toWrite, &written, portMAX_DELAY);
  i2sBufferPos += written / 2;
}

// =====================================================
// LED Control Task
// =====================================================
void vTaskWriteLEDs(void *pvParameters) {
  (void)pvParameters;
  uint64_t prevStable = 0;
  ledState = LED_ALL_OFF;
  write595_all(ledState);
  unsigned long resetButtonPressTime = 0;
  bool          resetButtonHeld      = false;
  bool          resetHoldFired       = false;
  uint64_t currentStable = 0;

  for (;;) {
    uint64_t stable;
    if (xQueueReceive(buttonQueue, &stable, pdMS_TO_TICKS(50))) {
      currentStable = stable;
      uint64_t pressed  = stable & ~prevStable;
      uint64_t released = prevStable & ~stable;

      if (pressed & (1ULL << QUIZ_BUTTON_NUM)) {
        quizMode ? exitQuizMode() : enterQuizMode();
      }

      if (pressed & (1ULL << RESET_BUTTON_NUM)) {
        resetButtonPressTime = millis();
        resetButtonHeld      = true;
        resetHoldFired       = false;
      }
      if (released & (1ULL << RESET_BUTTON_NUM)) {
        if (resetButtonHeld && !resetHoldFired) {
          resetBoard();
        }
        resetButtonHeld = false;
      }

      if (pressed & (1ULL << MODE_BUTTON_NUM)) {
        toggleRFIDMode();
      }

      if (pressed & (1ULL << LANG_BUTTON_NUM)) {
        SystemLanguage newLang = (currentLanguage == SYS_LANG_HINDI) ? SYS_LANG_ENGLISH : SYS_LANG_HINDI;
        Serial.print("Lang: toggling to ");
        Serial.println(languageNames[newLang]);
        selectLanguage(newLang);
      }

      for (uint8_t i = 0; i < 40; i++) {
        if (!(pressed & (1ULL << i))) continue;
        if (i == QUIZ_BUTTON_NUM || i == RESET_BUTTON_NUM || i == MODE_BUTTON_NUM || i == LANG_BUTTON_NUM) continue;
        if (rfidMode) continue;

        int8_t physical = buttonToPhysical[i];
        if (physical < 0) continue;
        if (quizMode) {
          if (quizState == QUIZ_WAITING_FOR_ANSWER && !playingScore)
            processAnswer(physical);
        } else {
          if (!playingScore) {
            ledState = LED_ALL_OFF;
            int led  = switchToLED[physical];
            if (led >= 0) ledState &= ~(1ULL << led);
            currentTrackIndex = physical;
            safeCloseFile(audioFile);
            String stateAudio = getSwitchAudioPath(physical);
            openAndStartAudio(stateAudio);
            write595_all(ledState);
            Serial.print("Switch "); Serial.print(physical);
            Serial.print(" -> "); Serial.println(stateAudio);
          }
        }
      }
      prevStable = stable;
    }

    if (resetButtonHeld && !resetHoldFired) {
      if (millis() - resetButtonPressTime >= BOSS_HOLD_TIME) {
        resetHoldFired = true;
        resetButtonHeld = false;
        flashResetLEDs();
        deleteSpeakerConfigAndRestart();
      }
    }
  }
}

// =====================================================
// setup() & loop()
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("================================================");
  Serial.println("   BHARAT QUIZ BOARD  —  HINDI + ENGLISH  v3.2  ");
  Serial.println("================================================");

  randomSeed(esp_random());
  initI2S();
  sdMutex = xSemaphoreCreateMutex();

  pinMode(PL, OUTPUT); pinMode(CP, OUTPUT); pinMode(Q7, INPUT);
  digitalWrite(PL, HIGH); digitalWrite(CP, LOW);
  pinMode(LATCH, OUTPUT); pinMode(CLOCK, OUTPUT); pinMode(DATA, OUTPUT);
  ledState = LED_ALL_OFF;
  write595_all(ledState);

  pinMode(BT_LED, OUTPUT);     digitalWrite(BT_LED, LOW);
  pinMode(CORRECT_LED, OUTPUT);
  pinMode(WRONG_LED,   OUTPUT);
  resetAnswerLEDs();

  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  // Force 16 MHz SPI Speed to eliminate audio pipeline stalling
  if (!SD.begin(SD_CS, sdSPI, 16000000)) {
    Serial.println("SD CARD FAILED");
    while (1);
  }
  Serial.println("SD CARD READY");

  currentLanguage = SYS_LANG_HINDI;
  Serial.println("Default language: Hindi");
  if (!loadBluetoothName()) {
    runSpeakerSetupPortal();
  }

  pinMode(RFID_RST, OUTPUT);
  digitalWrite(RFID_RST, LOW);
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfidInitialized = false;
  rfidMode        = false;
  Serial.println("RFID hardware ready (disabled by default)");
  buttonQueue = xQueueCreate(1, sizeof(uint64_t));
  xTaskCreatePinnedToCore(vTaskReadButtons, "Buttons", 4096, NULL, 2, NULL, 1);
#ifdef DEBUG_165
  xTaskCreatePinnedToCore(vTaskDebug165,   "DBG165",  4096, NULL, 1, NULL, 1);
#endif
  xTaskCreatePinnedToCore(vTaskWriteLEDs, "LEDs", 8192, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(vTaskRFID,      "RFID", 4096, NULL, 1, NULL, 0);
  a2dp_source.start(btSpeakerName, get_sound_data);
  a2dp_source.set_volume(80);
  a2dp_source.set_auto_reconnect(true);
  a2dp_source.set_on_connection_state_changed([](esp_a2d_connection_state_t state, void*) {
    const char* states[] = {"DISCONNECTED","CONNECTING","CONNECTED","DISCONNECTING"};
    Serial.print("BT: "); Serial.println(states[state]);
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
      if (!playedWelcome) {
        playedWelcome = true;
        setWelcomeLights(true);
        openAndStartAudio(getLocalizedAudio(welcomeAudioPaths), true);
      }
    }
  });
  Serial.print("Connecting to: "); Serial.println(btSpeakerName);
}

void loop() {
  digitalWrite(BT_LED, a2dp_source.is_connected() ? HIGH : LOW);
  processPendingAction(); 
  playLocalAudio();
  vTaskDelay(pdMS_TO_TICKS(10));
}
