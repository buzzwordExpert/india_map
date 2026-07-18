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
  {"/Hindi/up_wrong.wav",     "/English/up_wrong_eng.wav"},
  {"/Hindi/raj_wrong.wav",    "/English/raj_wrong_eng.wav"},
  {"/Hindi/bih_wrong.wav",    "/English/bih_wrong_eng.wav"},
  {"/Hindi/sik_wrong.wav",    "/English/sik_wrong_eng.wav"},
  {"/Hindi/hp_wrong.wav",     "/English/hp_wrong_eng.wav"},
  {"/Hindi/pun_wrong.wav",    "/English/pun_wrong_eng.wav"},
  {"/Hindi/uk_wrong.wav",     "/English/uk_wrong_eng.wav"},
  {"/Hindi/har_wrong.wav",    "/English/har_wrong_eng.wav"},
  {"/Hindi/tri_wrong.wav",    "/English/tri_wrong_eng.wav"},
  {"/Hindi/meg_wrong.wav",    "/English/meg_wrong_eng.wav"},
  {"/Hindi/ass_wrong.wav",    "/English/ass_wrong_eng.wav"},
  {"/Hindi/wb_wrong.wav",     "/English/wb_wrong_eng.wav"},
  {"/Hindi/aru_wrong.wav",    "/English/aru_wrong_eng.wav"},
  {"/Hindi/nag_wrong.wav",    "/English/nag_wrong_eng.wav"},
  {"/Hindi/man_wrong.wav",    "/English/man_wrong_eng.wav"},
  {"/Hindi/miz_wrong.wav",    "/English/miz_wrong_eng.wav"},
  {"/Hindi/jha_wrong.wav",    "/English/jha_wrong_eng.wav"},
  {"/Hindi/chh_wrong.wav",    "/English/chh_wrong_eng.wav"},
  {"/Hindi/mp_wrong.wav",     "/English/mp_wrong_eng.wav"},
  {"/Hindi/guj_wrong.wav",    "/English/guj_wrong_eng.wav"},
  {"/Hindi/odi_wrong.wav",    "/English/odi_wrong_eng.wav"},
  {"/Hindi/mah_wrong.wav",    "/English/mah_wrong_eng.wav"},
  {"/Hindi/tel_wrong.wav",    "/English/tel_wrong_eng.wav"},
  {"/Hindi/ap_wrong.wav",     "/English/ap_wrong_eng.wav"},
  {"/Hindi/goa_wrong.wav",    "/English/goa_wrong_eng.wav"},
  {"/Hindi/kar_wrong.wav",    "/English/kar_wrong_eng.wav"},
  {"/Hindi/ker_wrong.wav",    "/English/ker_wrong_eng.wav"},
  {"/Hindi/tn_wrong.wav",     "/English/tn_wrong_eng.wav"},
  {"/Hindi/lad_wrong.wav",    "/English/lad_wrong_eng.wav"},
  {"/Hindi/jk_wrong.wav",     "/English/jk_wrong_eng.wav"},
  {"/Hindi/chd_wrong.wav",    "/English/chd_wrong_eng.wav"},
  {"/Hindi/del_wrong.wav",    "/English/del_wrong_eng.wav"},
  {"/Hindi/lak_wrong.wav",    "/English/lak_wrong_eng.wav"},
  {"/Hindi/an_wrong.wav",     "/English/an_wrong_eng.wav"},
  {"/Hindi/dnhdd_wrong.wav",  "/English/dnhdd_wrong_eng.wav"},
  {"/Hindi/pud_wrong.wav",    "/English/pud_wrong_eng.wav"}
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
const char* const speakerSetupAudioPaths[SYS_LANG_COUNT] = { "/Hindi/speaker_hi.wav", "/English/speaker_eng.wav" };
const char* const mlSwitchQuizAudioPaths[SYS_LANG_COUNT] = { "/Hindi/mlswitch_hindi.wav", "/English/switch_quiz_mode.wav" };
const char* const mlRfidQuizAudioPaths[SYS_LANG_COUNT] = { "/Hindi/rfid_quiz_mode_hindi.wav", "/English/rfid_quiz_mode.wav" };
const char* const mlSwitchQuizFifthAudioPaths[SYS_LANG_COUNT] = { "/Hindi/quiz_mode_fifth_hi.wav", "/English/quiz_mode_fifth_en.wav" };
const char* const mlRfidQuizFifthAudioPaths[SYS_LANG_COUNT]   = { "/Hindi/rfid_quiz_mode_fifth_hi.wav",   "/English/rfid_quiz_mode_fifth_en.wav" };
const char* const mlSwitchQuizSixthAudioPaths[SYS_LANG_COUNT]   = { "/Hindi/quiz_mode_sixth_hi.wav",   "/English/quiz_mode_sixth_en.wav" };
const char* const mlRfidQuizSixthAudioPaths[SYS_LANG_COUNT]     = { "/Hindi/rfid_quiz_mode_sixth_hi.wav",   "/English/rfid_quiz_mode_sixth_en.wav" };
const char* const mlSwitchQuizSeventhAudioPaths[SYS_LANG_COUNT] = { "/Hindi/quiz_mode_seventh_hi.wav", "/English/quiz_mode_seventh_en.wav" };
const char* const mlRfidQuizSeventhAudioPaths[SYS_LANG_COUNT]   = { "/Hindi/rfid_quiz_mode_seventh_hi.wav", "/English/rfid_quiz_mode_seventh_en.wav" };
const char* const classSelectPromptAudioPaths[SYS_LANG_COUNT] = { "/Hindi/class_select_prompt_hi.wav", "/English/class_select_prompt_en.wav" };
const char* const resetAudioPaths[SYS_LANG_COUNT] = { "/Hindi/reset_h.wav", "/English/reset.wav" };
const char* const languageChangedAudioPaths[SYS_LANG_COUNT] = { "/Hindi/Hindi_lan.wav", "/English/English_lan.wav" };
const char* const scoreIntroAudioPaths[SYS_LANG_COUNT] = { "/Hindi/final_score_hindi.wav", "/English/final_score.wav" };
const char* const outOfAudioPaths[SYS_LANG_COUNT] = { "/Hindi/outof_hindi.wav", "/English/outof.wav" };

const char* correctWordAudio = "/correctword.wav";

// =====================================================
// NUMBER AUDIO
// =====================================================
const char* numberAudioFiles[88] = {
    "/Hindi/n0.wav", "/Hindi/n1.wav", "/Hindi/n2.wav", "/Hindi/n3.wav", "/Hindi/n4.wav",
    "/Hindi/n5.wav", "/Hindi/n6.wav", "/Hindi/n7.wav", "/Hindi/n8.wav", "/Hindi/n9.wav",
    "/Hindi/n10.wav", "/Hindi/n11.wav", "/Hindi/n12.wav", "/Hindi/n13.wav", "/Hindi/n14.wav",
    "/Hindi/n15.wav", "/Hindi/n16.wav", "/Hindi/n17.wav", "/Hindi/n18.wav", "/Hindi/n19.wav",
    "/Hindi/n20.wav", "/Hindi/n21.wav", "/Hindi/n22.wav", "/Hindi/n23.wav", "/Hindi/n24.wav",
    "/Hindi/n25.wav", "/Hindi/n26.wav", "/Hindi/n27.wav", "/Hindi/n28.wav", "/Hindi/n29.wav",
    "/Hindi/n30.wav", "/Hindi/n31.wav", "/Hindi/n32.wav", "/Hindi/n33.wav", "/Hindi/n34.wav",
    "/Hindi/n35.wav", "/Hindi/n36.wav", "/Hindi/n37.wav", "/Hindi/n38.wav", "/Hindi/n39.wav",
    "/Hindi/n40.wav", "/Hindi/n41.wav", "/Hindi/n42.wav", "/Hindi/n43.wav", "/Hindi/n44.wav",
    "/Hindi/n45.wav", "/Hindi/n46.wav", "/Hindi/n47.wav", "/Hindi/n48.wav", "/Hindi/n49.wav",
    "/Hindi/n50.wav", "/Hindi/n51.wav", "/Hindi/n52.wav", "/Hindi/n53.wav", "/Hindi/n54.wav",
    "/Hindi/n55.wav", "/Hindi/n56.wav", "/Hindi/n57.wav", "/Hindi/n58.wav", "/Hindi/n59.wav",
    "/Hindi/n60.wav", "/Hindi/n61.wav", "/Hindi/n62.wav", "/Hindi/n63.wav", "/Hindi/n64.wav",
    "/Hindi/n65.wav", "/Hindi/n66.wav", "/Hindi/n67.wav", "/Hindi/n68.wav", "/Hindi/n69.wav",
    "/Hindi/n70.wav", "/Hindi/n71.wav", "/Hindi/n72.wav", "/Hindi/n73.wav", "/Hindi/n74.wav",
    "/Hindi/n75.wav", "/Hindi/n76.wav", "/Hindi/n77.wav", "/Hindi/n78.wav", "/Hindi/n79.wav",
    "/Hindi/n80.wav", "/Hindi/n81.wav", "/Hindi/n82.wav", "/Hindi/n83.wav", "/Hindi/n84.wav",
    "/Hindi/n85.wav", "/Hindi/n86.wav", "/Hindi/n87.wav",
};

String getNumberAudioPath(int n) {
  const int hindiCount = sizeof(numberAudioFiles) / sizeof(numberAudioFiles[0]); // 88
  if (n < 0) return String("");

  if (currentLanguage != SYS_LANG_HINDI) {
    String ep = String("/") + languageFolders[currentLanguage] + "/n" + String(n) + ".wav";
    if (SD.exists(ep.c_str())) return ep;
  }
  if (n >= hindiCount) return String("");   // no clip available, but no crash
  return String(numberAudioFiles[n]);
}

// =====================================================
// QUIZ ANSWER KEY
// =====================================================
const std::vector<int8_t> correctAnswers[87] = 
{ 
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

const std::vector<int8_t> correctAnswersFifth[125] = 
{
  {2}, {5}, {6}, {25}, {20}, {19}, {19}, {21}, {27}, {1}, // Q1 - Q10
  {6}, {19}, {10}, {1}, {23}, {27}, {25}, {22}, {2}, {25}, // Q11 - Q20
  {27}, {25}, {20}, {18}, {1}, {21}, {21}, {25}, {27}, {5}, // Q21 - Q30
  {11}, {1}, {19}, {18}, {19}, {1}, {10}, {19}, {11}, {6}, // Q31 - Q40
  {14}, {20}, {6}, {25}, {20}, {6}, {10}, {0}, {11}, {7}, // Q41 - Q50
  {22}, {29}, {25}, {5}, {27}, {19}, {1}, {27}, {25}, {6}, // Q51 - Q60
  {19}, {25}, {21}, {1}, {6}, {5}, {19}, {19}, {18}, {26}, // Q61 - Q70
  {27}, {6, 25}, {25, 21, 18, 6}, {5, 7, 0}, {11, 20, 23, 27}, // Q71 - Q75
  {27, 26, 25, 11, 20, 18, 6}, {6, 4, 29}, {27, 26, 23, 20, 14, 0}, {10, 26, 27}, {25, 10, 11}, // Q76 - Q80
  {1, 19}, {15, 8, 13, 10}, {27, 25, 22}, {10, 11}, {21, 18}, // Q81 - Q85
  {31}, {33}, {35}, {34}, {31}, // Q86 - Q90
  {33}, {33}, {32}, {28}, {32}, // Q91 - Q95
  {29}, {28}, {29}, {28}, {28}, // Q96 - Q100
  {32}, {33}, {33}, {28}, {28}, // Q101 - Q105
  {28}, {28}, {31}, {33}, {35}, // Q106 - Q110
  {33, 32, 35, 34, 29}, {35, 34}, {29, 28}, {33, 32}, {28, 29}, // Q111 - Q115
  {33}, {28}, {28}, {33}, {29}, // Q116 - Q120
  {31, 29, 35}, {33, 32}, {29, 28}, {35, 34}, {31, 33} // Q121 - Q125
};

const std::vector<int8_t> correctAnswersSixth[99] =
{
  {6, 0, 2, 16, 11}, {6, 7, 31, 0}, {18, 21, 19}, {21, 22, 23}, {21, 25, 22, 23}, {25, 27}, {12, 10}, {19, 21, 24, 25, 26}, {11, 20, 23, 27}, {29, 4, 6, 3, 12}, // Q1 - Q10
  {19, 7, 5, 1}, {19}, {19, 5, 7}, {5, 19}, {19, 7, 5, 1}, {0, 2, 18, 5, 7}, {2}, {0}, {18}, {7}, // Q11 - Q20
  {19, 20, 25, 23, 31}, {18, 0, 2}, {2, 0, 18, 20}, {2}, {19}, {11}, {6}, {19}, {2}, {10}, // Q21 - Q30
  {19, 21, 18, 22}, {19, 21, 24, 25, 26, 27}, {19}, {19}, {7}, {1}, {7}, {2}, {0}, {18}, // Q31 - Q40
  {2}, {0}, {2}, {18}, {2}, {2}, {0}, {19}, {6}, {2}, // Q41 - Q50
  {10}, {19}, {1}, {6}, {20}, {26}, {21}, {11}, {7}, {19}, // Q51 - Q60
  {23}, {21}, {22}, {8}, {26}, {1}, {18}, {14}, {14}, {10}, // Q61 - Q70
  {33}, {11}, {18}, {25}, {2}, {26}, // Q71 - Q76 (Q77 skipped)
  {32}, {35}, {33}, {33}, {28}, {33}, {32}, {28}, {35}, {29}, // Q78 - Q87
  {34}, {28}, {29}, {33}, {28}, {32}, {28}, {32}, {31}, {32}, // Q88 - Q97
  {31, 28, 32, 33}, {31, 35, 28}, {28, 33, 35, 31} // Q98 - Q100
};

const std::vector<int8_t> correctAnswersSeventh[129] =
{
  {5}, {26}, {1}, {1}, {10}, {11}, {21}, {0}, {0}, {27}, // Q1 - Q10
  {27}, {22}, {25}, {25}, {25}, {20}, {20}, {20}, {19}, {19}, // Q11 - Q20
  {4}, {4}, {29}, {29}, {18}, {18}, {2}, {2}, {17}, {16}, // Q21 - Q30
  {16}, {15}, {14}, {8}, {26}, {11}, {14}, {26}, {23}, {23}, // Q31 - Q40
  {13}, {3}, {14}, {26}, {12}, {8}, {12}, {24}, {24}, {6}, // Q41 - Q50
  {25}, {6}, {6}, {21}, {33}, {5}, {5}, {2}, {27}, {25}, // Q51 - Q60
  {1}, {20}, {2}, {19}, {19}, {5}, {5}, {11}, {19}, {0}, // Q61 - Q70
  {7}, {7}, {7}, {7}, {7}, {6}, {5, 7}, {10, 11}, {25, 26, 27}, {19, 1, 18, 17, 16, 11, 8, 15}, // Q71 - Q80
  {1, 19}, {19, 21, 24, 25, 26}, {25, 10, 11}, {15, 8, 13}, {10, 11}, {5, 7, 0}, {19, 21, 22}, {11, 10, 2}, {21, 19, 18}, {27, 25, 23, 20}, // Q81 - Q90
  {26, 25, 24, 27}, {28}, {32}, {31}, {30}, {28}, {33}, {35}, {31}, {34}, // Q91 - Q100
  {29}, {35}, {29}, {30}, {35}, {28}, {33}, {32}, {33}, {30}, // Q101 - Q111 (Q105 skipped)
  {29}, {28}, {31}, {28}, {32}, {31, 29, 35}, {33, 32}, {35, 32, 33, 34}, {28, 33}, {31, 30, 34, 29, 29, 28, 35, 33}, // Q112 - Q121
  {32, 33}, {32, 33}, {28, 29}, {35, 34}, {28}, {32}, {28}, {33}, {34} // Q122 - Q130

};

const uint8_t TOTAL_QUESTIONS = 87;
bool questionsAsked[87]  = {false};
int  totalQuestionsAsked = 0;

const uint8_t TOTAL_QUESTIONS_FIFTH = 125;
const uint8_t TOTAL_QUESTIONS_SIXTH   = 99;
const uint8_t TOTAL_QUESTIONS_SEVENTH = 129;
bool questionsAskedFifth[125] = {false};
bool questionsAskedSixth[99]   = {false};
bool questionsAskedSeventh[129] = {false};

// =====================================================
// QUIZ CLASS SELECTION
//   QUIZ_CLASS_NONE = original 36-state quiz (single quiz-button click)
//   QUIZ_CLASS_5/6/7 = grade-specific question banks (double-click quiz
//   button, then press Reset/Mode/Lang to pick the class)
// =====================================================
enum QuizClass {
  QUIZ_CLASS_NONE = 0,
  QUIZ_CLASS_5    = 5,
  QUIZ_CLASS_6    = 6,
  QUIZ_CLASS_7    = 7
};
QuizClass activeQuizClass = QUIZ_CLASS_NONE;

// =====================================================
// QUIZ QUESTIONS — 87 entries × 2 languages
// =====================================================
const char* const quizQuestions[87][SYS_LANG_COUNT] = {
  {"/Hindi/q1_hi.wav",  "/English/q1_en.wav"},  {"/Hindi/q2_hi.wav",  "/English/q2_en.wav"},
  {"/Hindi/q3_hi.wav",  "/English/q3_en.wav"},  {"/Hindi/q4_hi.wav",  "/English/q4_en.wav"},
  {"/Hindi/q5_hi.wav",  "/English/q5_en.wav"},  {"/Hindi/q6_hi.wav",  "/English/q6_en.wav"},
  {"/Hindi/q7_hi.wav",  "/English/q7_en.wav"},  {"/Hindi/q8_hi.wav",  "/English/q8_en.wav"},
  {"/Hindi/q9_hi.wav",  "/English/q9_en.wav"},  {"/Hindi/q10_hi.wav", "/English/q10_en.wav"},
  {"/Hindi/q11_hi.wav", "/English/q11_en.wav"}, {"/Hindi/q12_hi.wav", "/English/q12_en.wav"},
  {"/Hindi/q13_hi.wav", "/English/q13_en.wav"}, {"/Hindi/q14_hi.wav", "/English/q14_en.wav"},
  {"/Hindi/q15_hi.wav", "/English/q15_en.wav"}, {"/Hindi/q16_hi.wav", "/English/q16_en.wav"},
  {"/Hindi/q17_hi.wav", "/English/q17_en.wav"}, {"/Hindi/q18_hi.wav", "/English/q18_en.wav"},
  {"/Hindi/q19_hi.wav", "/English/q19_en.wav"}, {"/Hindi/q20_hi.wav", "/English/q20_en.wav"},
  {"/Hindi/q21_hi.wav", "/English/q21_en.wav"}, {"/Hindi/q22_hi.wav", "/English/q22_en.wav"},
  {"/Hindi/q23_hi.wav", "/English/q23_en.wav"}, {"/Hindi/q24_hi.wav", "/English/q24_en.wav"},
  {"/Hindi/q25_hi.wav", "/English/q25_en.wav"}, {"/Hindi/q26_hi.wav", "/English/q26_en.wav"},
  {"/Hindi/q27_hi.wav", "/English/q27_en.wav"}, {"/Hindi/q28_hi.wav", "/English/q28_en.wav"},
  {"/Hindi/q29_hi.wav", "/English/q29_en.wav"}, {"/Hindi/q30_hi.wav", "/English/q30_en.wav"},
  {"/Hindi/q31_hi.wav", "/English/q31_en.wav"}, {"/Hindi/q32_hi.wav", "/English/q32_en.wav"},
  {"/Hindi/q33_hi.wav", "/English/q33_en.wav"}, {"/Hindi/q34_hi.wav", "/English/q34_en.wav"},
  {"/Hindi/q35_hi.wav", "/English/q35_en.wav"}, {"/Hindi/q36_hi.wav", "/English/q36_en.wav"},
  {"/Hindi/q37_hi.wav", "/English/q37_en.wav"}, {"/Hindi/q38_hi.wav", "/English/q38_en.wav"},
  {"/Hindi/q39_hi.wav", "/English/q39_en.wav"}, {"/Hindi/q40_hi.wav", "/English/q40_en.wav"},
  {"/Hindi/q41_hi.wav", "/English/q41_en.wav"}, {"/Hindi/q42_hi.wav", "/English/q42_en.wav"},
  {"/Hindi/q43_hi.wav", "/English/q43_en.wav"}, {"/Hindi/q44_hi.wav", "/English/q44_en.wav"},
  {"/Hindi/q45_hi.wav", "/English/q45_en.wav"}, {"/Hindi/q46_hi.wav", "/English/q46_en.wav"},
  {"/Hindi/q47_hi.wav", "/English/q47_en.wav"}, {"/Hindi/q48_hi.wav", "/English/q48_en.wav"},
  {"/Hindi/q49_hi.wav", "/English/q49_en.wav"}, {"/Hindi/q50_hi.wav", "/English/q50_en.wav"},
  {"/Hindi/q51_hi.wav", "/English/q51_en.wav"}, {"/Hindi/q52_hi.wav", "/English/q52_en.wav"},
  {"/Hindi/q53_hi.wav", "/English/q53_en.wav"}, {"/Hindi/q54_hi.wav", "/English/q54_en.wav"},
  {"/Hindi/q55_hi.wav", "/English/q55_en.wav"}, {"/Hindi/q56_hi.wav", "/English/q56_en.wav"},
  {"/Hindi/q57_hi.wav", "/English/q57_en.wav"}, {"/Hindi/q58_hi.wav", "/English/q58_en.wav"},
  {"/Hindi/q59_hi.wav", "/English/q59_en.wav"}, {"/Hindi/q60_hi.wav", "/English/q60_en.wav"},
  {"/Hindi/q61_hi.wav", "/English/q61_en.wav"}, {"/Hindi/q62_hi.wav", "/English/q62_en.wav"},
  {"/Hindi/q63_hi.wav", "/English/q63_en.wav"}, {"/Hindi/q64_hi.wav", "/English/q64_en.wav"},
  {"/Hindi/q65_hi.wav", "/English/q65_en.wav"}, {"/Hindi/q66_hi.wav", "/English/q66_en.wav"},
  {"/Hindi/q67_hi.wav", "/English/q67_en.wav"}, {"/Hindi/q68_hi.wav", "/English/q68_en.wav"},
  {"/Hindi/q69_hi.wav", "/English/q69_en.wav"}, {"/Hindi/q70_hi.wav", "/English/q70_en.wav"},
  {"/Hindi/q71_hi.wav", "/English/q71_en.wav"}, {"/Hindi/q72_hi.wav", "/English/q72_en.wav"},
  {"/Hindi/q73_hi.wav", "/English/q73_en.wav"}, {"/Hindi/q74_hi.wav", "/English/q74_en.wav"},
  {"/Hindi/q75_hi.wav", "/English/q75_en.wav"}, {"/Hindi/q76_hi.wav", "/English/q76_en.wav"},
  {"/Hindi/q77_hi.wav", "/English/q77_en.wav"}, {"/Hindi/q78_hi.wav", "/English/q78_en.wav"},
  {"/Hindi/q79_hi.wav", "/English/q79_en.wav"}, {"/Hindi/q80_hi.wav", "/English/q80_en.wav"},
  {"/Hindi/q81_hi.wav", "/English/q81_en.wav"}, {"/Hindi/q82_hi.wav", "/English/q82_en.wav"},
  {"/Hindi/q83_hi.wav", "/English/q83_en.wav"}, {"/Hindi/q84_hi.wav", "/English/q84_en.wav"},
  {"/Hindi/q85_hi.wav", "/English/q85_en.wav"}, {"/Hindi/q86_hi.wav", "/English/q86_en.wav"},
  {"/Hindi/q87_hi.wav", "/English/q87_en.wav"},
};

const char* const quizQuestionsFifth[125][SYS_LANG_COUNT] =
{
  {"/Hindi/Class_5/q1_hi.wav","/English/Class_5/q1_en.wav"}, {"/Hindi/Class_5/q2_hi.wav","/English/Class_5/q2_en.wav"},
  {"/Hindi/Class_5/q3_hi.wav","/English/Class_5/q3_en.wav"}, {"/Hindi/Class_5/q4_hi.wav","/English/Class_5/q4_en.wav"},
  {"/Hindi/Class_5/q5_hi.wav","/English/Class_5/q5_en.wav"}, {"/Hindi/Class_5/q6_hi.wav","/English/Class_5/q6_en.wav"},
  {"/Hindi/Class_5/q7_hi.wav","/English/Class_5/q7_en.wav"}, {"/Hindi/Class_5/q8_hi.wav","/English/Class_5/q8_en.wav"},
  {"/Hindi/Class_5/q9_hi.wav","/English/Class_5/q9_en.wav"}, {"/Hindi/Class_5/q10_hi.wav","/English/Class_5/q10_en.wav"},
  {"/Hindi/Class_5/q11_hi.wav","/English/Class_5/q11_en.wav"}, {"/Hindi/Class_5/q12_hi.wav","/English/Class_5/q12_en.wav"},
  {"/Hindi/Class_5/q13_hi.wav","/English/Class_5/q13_en.wav"}, {"/Hindi/Class_5/q14_hi.wav","/English/Class_5/q14_en.wav"},
  {"/Hindi/Class_5/q15_hi.wav","/English/Class_5/q15_en.wav"}, {"/Hindi/Class_5/q16_hi.wav","/English/Class_5/q16_en.wav"},
  {"/Hindi/Class_5/q17_hi.wav","/English/Class_5/q17_en.wav"}, {"/Hindi/Class_5/q18_hi.wav","/English/Class_5/q18_en.wav"},
  {"/Hindi/Class_5/q19_hi.wav","/English/Class_5/q19_en.wav"}, {"/Hindi/Class_5/q20_hi.wav","/English/Class_5/q20_en.wav"},
  {"/Hindi/Class_5/q21_hi.wav","/English/Class_5/q21_en.wav"}, {"/Hindi/Class_5/q22_hi.wav","/English/Class_5/q22_en.wav"},
  {"/Hindi/Class_5/q23_hi.wav","/English/Class_5/q23_en.wav"}, {"/Hindi/Class_5/q24_hi.wav","/English/Class_5/q24_en.wav"},
  {"/Hindi/Class_5/q25_hi.wav","/English/Class_5/q25_en.wav"}, {"/Hindi/Class_5/q26_hi.wav","/English/Class_5/q26_en.wav"},
  {"/Hindi/Class_5/q27_hi.wav","/English/Class_5/q27_en.wav"}, {"/Hindi/Class_5/q28_hi.wav","/English/Class_5/q28_en.wav"},
  {"/Hindi/Class_5/q29_hi.wav","/English/Class_5/q29_en.wav"}, {"/Hindi/Class_5/q30_hi.wav","/English/Class_5/q30_en.wav"},
  {"/Hindi/Class_5/q31_hi.wav","/English/Class_5/q31_en.wav"}, {"/Hindi/Class_5/q32_hi.wav","/English/Class_5/q32_en.wav"},
  {"/Hindi/Class_5/q33_hi.wav","/English/Class_5/q33_en.wav"}, {"/Hindi/Class_5/q34_hi.wav","/English/Class_5/q34_en.wav"},
  {"/Hindi/Class_5/q35_hi.wav","/English/Class_5/q35_en.wav"}, {"/Hindi/Class_5/q36_hi.wav","/English/Class_5/q36_en.wav"},
  {"/Hindi/Class_5/q37_hi.wav","/English/Class_5/q37_en.wav"}, {"/Hindi/Class_5/q38_hi.wav","/English/Class_5/q38_en.wav"},
  {"/Hindi/Class_5/q39_hi.wav","/English/Class_5/q39_en.wav"}, {"/Hindi/Class_5/q40_hi.wav","/English/Class_5/q40_en.wav"},
  {"/Hindi/Class_5/q41_hi.wav","/English/Class_5/q41_en.wav"}, {"/Hindi/Class_5/q42_hi.wav","/English/Class_5/q42_en.wav"},
  {"/Hindi/Class_5/q43_hi.wav","/English/Class_5/q43_en.wav"}, {"/Hindi/Class_5/q44_hi.wav","/English/Class_5/q44_en.wav"},
  {"/Hindi/Class_5/q45_hi.wav","/English/Class_5/q45_en.wav"}, {"/Hindi/Class_5/q46_hi.wav","/English/Class_5/q46_en.wav"},
  {"/Hindi/Class_5/q47_hi.wav","/English/Class_5/q47_en.wav"}, {"/Hindi/Class_5/q48_hi.wav","/English/Class_5/q48_en.wav"},
  {"/Hindi/Class_5/q49_hi.wav","/English/Class_5/q49_en.wav"}, {"/Hindi/Class_5/q50_hi.wav","/English/Class_5/q50_en.wav"},
  {"/Hindi/Class_5/q51_hi.wav","/English/Class_5/q51_en.wav"}, {"/Hindi/Class_5/q52_hi.wav","/English/Class_5/q52_en.wav"},
  {"/Hindi/Class_5/q53_hi.wav","/English/Class_5/q53_en.wav"}, {"/Hindi/Class_5/q54_hi.wav","/English/Class_5/q54_en.wav"},
  {"/Hindi/Class_5/q55_hi.wav","/English/Class_5/q55_en.wav"}, {"/Hindi/Class_5/q56_hi.wav","/English/Class_5/q56_en.wav"},
  {"/Hindi/Class_5/q57_hi.wav","/English/Class_5/q57_en.wav"}, {"/Hindi/Class_5/q58_hi.wav","/English/Class_5/q58_en.wav"},
  {"/Hindi/Class_5/q59_hi.wav","/English/Class_5/q59_en.wav"}, {"/Hindi/Class_5/q60_hi.wav","/English/Class_5/q60_en.wav"},
  {"/Hindi/Class_5/q61_hi.wav","/English/Class_5/q61_en.wav"}, {"/Hindi/Class_5/q62_hi.wav","/English/Class_5/q62_en.wav"},
  {"/Hindi/Class_5/q63_hi.wav","/English/Class_5/q63_en.wav"}, {"/Hindi/Class_5/q64_hi.wav","/English/Class_5/q64_en.wav"},
  {"/Hindi/Class_5/q65_hi.wav","/English/Class_5/q65_en.wav"}, {"/Hindi/Class_5/q66_hi.wav","/English/Class_5/q66_en.wav"},
  {"/Hindi/Class_5/q67_hi.wav","/English/Class_5/q67_en.wav"}, {"/Hindi/Class_5/q68_hi.wav","/English/Class_5/q68_en.wav"},
  {"/Hindi/Class_5/q69_hi.wav","/English/Class_5/q69_en.wav"}, {"/Hindi/Class_5/q70_hi.wav","/English/Class_5/q70_en.wav"},
  {"/Hindi/Class_5/q71_hi.wav","/English/Class_5/q71_en.wav"}, {"/Hindi/Class_5/q72_hi.wav","/English/Class_5/q72_en.wav"},
  {"/Hindi/Class_5/q73_hi.wav","/English/Class_5/q73_en.wav"}, {"/Hindi/Class_5/q74_hi.wav","/English/Class_5/q74_en.wav"},
  {"/Hindi/Class_5/q75_hi.wav","/English/Class_5/q75_en.wav"}, {"/Hindi/Class_5/q76_hi.wav","/English/Class_5/q76_en.wav"},
  {"/Hindi/Class_5/q77_hi.wav","/English/Class_5/q77_en.wav"}, {"/Hindi/Class_5/q78_hi.wav","/English/Class_5/q78_en.wav"},
  {"/Hindi/Class_5/q79_hi.wav","/English/Class_5/q79_en.wav"}, {"/Hindi/Class_5/q80_hi.wav","/English/Class_5/q80_en.wav"},
  {"/Hindi/Class_5/q81_hi.wav","/English/Class_5/q81_en.wav"}, {"/Hindi/Class_5/q82_hi.wav","/English/Class_5/q82_en.wav"},
  {"/Hindi/Class_5/q83_hi.wav","/English/Class_5/q83_en.wav"}, {"/Hindi/Class_5/q84_hi.wav","/English/Class_5/q84_en.wav"},
  {"/Hindi/Class_5/q85_hi.wav","/English/Class_5/q85_en.wav"}, {"/Hindi/Class_5/q86_hi.wav","/English/Class_5/q86_en.wav"},
  {"/Hindi/Class_5/q87_hi.wav","/English/Class_5/q87_en.wav"}, {"/Hindi/Class_5/q88_hi.wav","/English/Class_5/q88_en.wav"},
  {"/Hindi/Class_5/q89_hi.wav","/English/Class_5/q89_en.wav"}, {"/Hindi/Class_5/q90_hi.wav","/English/Class_5/q90_en.wav"},
  {"/Hindi/Class_5/q91_hi.wav","/English/Class_5/q91_en.wav"}, {"/Hindi/Class_5/q92_hi.wav","/English/Class_5/q92_en.wav"},
  {"/Hindi/Class_5/q93_hi.wav","/English/Class_5/q93_en.wav"}, {"/Hindi/Class_5/q94_hi.wav","/English/Class_5/q94_en.wav"},
  {"/Hindi/Class_5/q95_hi.wav","/English/Class_5/q95_en.wav"}, {"/Hindi/Class_5/q96_hi.wav","/English/Class_5/q96_en.wav"},
  {"/Hindi/Class_5/q97_hi.wav","/English/Class_5/q97_en.wav"}, {"/Hindi/Class_5/q98_hi.wav","/English/Class_5/q98_en.wav"},
  {"/Hindi/Class_5/q99_hi.wav","/English/Class_5/q99_en.wav"}, {"/Hindi/Class_5/q100_hi.wav","/English/Class_5/q100_en.wav"},
  {"/Hindi/Class_5/q101_hi.wav","/English/Class_5/q101_en.wav"}, {"/Hindi/Class_5/q102_hi.wav","/English/Class_5/q102_en.wav"},
  {"/Hindi/Class_5/q103_hi.wav","/English/Class_5/q103_en.wav"}, {"/Hindi/Class_5/q104_hi.wav","/English/Class_5/q104_en.wav"},
  {"/Hindi/Class_5/q105_hi.wav","/English/Class_5/q105_en.wav"}, {"/Hindi/Class_5/q106_hi.wav","/English/Class_5/q106_en.wav"},
  {"/Hindi/Class_5/q107_hi.wav","/English/Class_5/q107_en.wav"}, {"/Hindi/Class_5/q108_hi.wav","/English/Class_5/q108_en.wav"},
  {"/Hindi/Class_5/q109_hi.wav","/English/Class_5/q109_en.wav"}, {"/Hindi/Class_5/q110_hi.wav","/English/Class_5/q110_en.wav"},
  {"/Hindi/Class_5/q111_hi.wav","/English/Class_5/q111_en.wav"}, {"/Hindi/Class_5/q112_hi.wav","/English/Class_5/q112_en.wav"},
  {"/Hindi/Class_5/q113_hi.wav","/English/Class_5/q113_en.wav"}, {"/Hindi/Class_5/q114_hi.wav","/English/Class_5/q114_en.wav"},
  {"/Hindi/Class_5/q115_hi.wav","/English/Class_5/q115_en.wav"}, {"/Hindi/Class_5/q116_hi.wav","/English/Class_5/q116_en.wav"},
  {"/Hindi/Class_5/q117_hi.wav","/English/Class_5/q117_en.wav"}, {"/Hindi/Class_5/q118_hi.wav","/English/Class_5/q118_en.wav"},
  {"/Hindi/Class_5/q119_hi.wav","/English/Class_5/q119_en.wav"}, {"/Hindi/Class_5/q120_hi.wav","/English/Class_5/q120_en.wav"},
  {"/Hindi/Class_5/q121_hi.wav","/English/Class_5/q121_en.wav"}, {"/Hindi/Class_5/q122_hi.wav","/English/Class_5/q122_en.wav"},
  {"/Hindi/Class_5/q123_hi.wav","/English/Class_5/q123_en.wav"}, {"/Hindi/Class_5/q124_hi.wav","/English/Class_5/q124_en.wav"},
  {"/Hindi/Class_5/q125_hi.wav","/English/Class_5/q125_en.wav"},
};

const char* const quizQuestionsSixth[99][SYS_LANG_COUNT] =
{
  {"/Hindi/Class_6/q1_hi.wav","/English/Class_6/q1_en.wav"},
  {"/Hindi/Class_6/q2_hi.wav","/English/Class_6/q2_en.wav"},
  {"/Hindi/Class_6/q3_hi.wav","/English/Class_6/q3_en.wav"},
  {"/Hindi/Class_6/q4_hi.wav","/English/Class_6/q4_en.wav"},
  {"/Hindi/Class_6/q5_hi.wav","/English/Class_6/q5_en.wav"},
  {"/Hindi/Class_6/q6_hi.wav","/English/Class_6/q6_en.wav"},
  {"/Hindi/Class_6/q7_hi.wav","/English/Class_6/q7_en.wav"},
  {"/Hindi/Class_6/q8_hi.wav","/English/Class_6/q8_en.wav"},
  {"/Hindi/Class_6/q9_hi.wav","/English/Class_6/q9_en.wav"},
  {"/Hindi/Class_6/q10_hi.wav","/English/Class_6/q10_en.wav"},
  {"/Hindi/Class_6/q11_hi.wav","/English/Class_6/q11_en.wav"},
  {"/Hindi/Class_6/q12_hi.wav","/English/Class_6/q12_en.wav"},
  {"/Hindi/Class_6/q13_hi.wav","/English/Class_6/q13_en.wav"},
  {"/Hindi/Class_6/q14_hi.wav","/English/Class_6/q14_en.wav"},
  {"/Hindi/Class_6/q15_hi.wav","/English/Class_6/q15_en.wav"},
  {"/Hindi/Class_6/q16_hi.wav","/English/Class_6/q16_en.wav"},
  {"/Hindi/Class_6/q17_hi.wav","/English/Class_6/q17_en.wav"},
  {"/Hindi/Class_6/q18_hi.wav","/English/Class_6/q18_en.wav"},
  {"/Hindi/Class_6/q19_hi.wav","/English/Class_6/q19_en.wav"},
  {"/Hindi/Class_6/q20_hi.wav","/English/Class_6/q20_en.wav"},
  {"/Hindi/Class_6/q21_hi.wav","/English/Class_6/q21_en.wav"},
  {"/Hindi/Class_6/q22_hi.wav","/English/Class_6/q22_en.wav"},
  {"/Hindi/Class_6/q23_hi.wav","/English/Class_6/q23_en.wav"},
  {"/Hindi/Class_6/q24_hi.wav","/English/Class_6/q24_en.wav"},
  {"/Hindi/Class_6/q25_hi.wav","/English/Class_6/q25_en.wav"},
  {"/Hindi/Class_6/q26_hi.wav","/English/Class_6/q26_en.wav"},
  {"/Hindi/Class_6/q27_hi.wav","/English/Class_6/q27_en.wav"},
  {"/Hindi/Class_6/q28_hi.wav","/English/Class_6/q28_en.wav"},
  {"/Hindi/Class_6/q29_hi.wav","/English/Class_6/q29_en.wav"},
  {"/Hindi/Class_6/q30_hi.wav","/English/Class_6/q30_en.wav"},
  {"/Hindi/Class_6/q31_hi.wav","/English/Class_6/q31_en.wav"},
  {"/Hindi/Class_6/q32_hi.wav","/English/Class_6/q32_en.wav"},
  {"/Hindi/Class_6/q33_hi.wav","/English/Class_6/q33_en.wav"},
  {"/Hindi/Class_6/q34_hi.wav","/English/Class_6/q34_en.wav"},
  {"/Hindi/Class_6/q35_hi.wav","/English/Class_6/q35_en.wav"},
  {"/Hindi/Class_6/q36_hi.wav","/English/Class_6/q36_en.wav"},
  {"/Hindi/Class_6/q37_hi.wav","/English/Class_6/q37_en.wav"},
  {"/Hindi/Class_6/q38_hi.wav","/English/Class_6/q38_en.wav"},
  {"/Hindi/Class_6/q39_hi.wav","/English/Class_6/q39_en.wav"},
  {"/Hindi/Class_6/q40_hi.wav","/English/Class_6/q40_en.wav"},
  {"/Hindi/Class_6/q41_hi.wav","/English/Class_6/q41_en.wav"},
  {"/Hindi/Class_6/q42_hi.wav","/English/Class_6/q42_en.wav"},
  {"/Hindi/Class_6/q43_hi.wav","/English/Class_6/q43_en.wav"},
  {"/Hindi/Class_6/q44_hi.wav","/English/Class_6/q44_en.wav"},
  {"/Hindi/Class_6/q45_hi.wav","/English/Class_6/q45_en.wav"},
  {"/Hindi/Class_6/q46_hi.wav","/English/Class_6/q46_en.wav"},
  {"/Hindi/Class_6/q47_hi.wav","/English/Class_6/q47_en.wav"},
  {"/Hindi/Class_6/q48_hi.wav","/English/Class_6/q48_en.wav"},
  {"/Hindi/Class_6/q49_hi.wav","/English/Class_6/q49_en.wav"},
  {"/Hindi/Class_6/q50_hi.wav","/English/Class_6/q50_en.wav"},
  {"/Hindi/Class_6/q51_hi.wav","/English/Class_6/q51_en.wav"},
  {"/Hindi/Class_6/q52_hi.wav","/English/Class_6/q52_en.wav"},
  {"/Hindi/Class_6/q53_hi.wav","/English/Class_6/q53_en.wav"},
  {"/Hindi/Class_6/q54_hi.wav","/English/Class_6/q54_en.wav"},
  {"/Hindi/Class_6/q55_hi.wav","/English/Class_6/q55_en.wav"},
  {"/Hindi/Class_6/q56_hi.wav","/English/Class_6/q56_en.wav"},
  {"/Hindi/Class_6/q57_hi.wav","/English/Class_6/q57_en.wav"},
  {"/Hindi/Class_6/q58_hi.wav","/English/Class_6/q58_en.wav"},
  {"/Hindi/Class_6/q59_hi.wav","/English/Class_6/q59_en.wav"},
  {"/Hindi/Class_6/q60_hi.wav","/English/Class_6/q60_en.wav"},
  {"/Hindi/Class_6/q61_hi.wav","/English/Class_6/q61_en.wav"},
  {"/Hindi/Class_6/q62_hi.wav","/English/Class_6/q62_en.wav"},
  {"/Hindi/Class_6/q63_hi.wav","/English/Class_6/q63_en.wav"},
  {"/Hindi/Class_6/q64_hi.wav","/English/Class_6/q64_en.wav"},
  {"/Hindi/Class_6/q65_hi.wav","/English/Class_6/q65_en.wav"},
  {"/Hindi/Class_6/q66_hi.wav","/English/Class_6/q66_en.wav"},
  {"/Hindi/Class_6/q67_hi.wav","/English/Class_6/q67_en.wav"},
  {"/Hindi/Class_6/q68_hi.wav","/English/Class_6/q68_en.wav"},
  {"/Hindi/Class_6/q69_hi.wav","/English/Class_6/q69_en.wav"},
  {"/Hindi/Class_6/q70_hi.wav","/English/Class_6/q70_en.wav"},
  {"/Hindi/Class_6/q71_hi.wav","/English/Class_6/q71_en.wav"},
  {"/Hindi/Class_6/q72_hi.wav","/English/Class_6/q72_en.wav"},
  {"/Hindi/Class_6/q73_hi.wav","/English/Class_6/q73_en.wav"},
  {"/Hindi/Class_6/q74_hi.wav","/English/Class_6/q74_en.wav"},
  {"/Hindi/Class_6/q75_hi.wav","/English/Class_6/q75_en.wav"},
  {"/Hindi/Class_6/q76_hi.wav","/English/Class_6/q76_en.wav"},
  {"/Hindi/Class_6/q77_hi.wav","/English/Class_6/q77_en.wav"},
  {"/Hindi/Class_6/q78_hi.wav","/English/Class_6/q78_en.wav"},
  {"/Hindi/Class_6/q79_hi.wav","/English/Class_6/q79_en.wav"},
  {"/Hindi/Class_6/q80_hi.wav","/English/Class_6/q80_en.wav"},
  {"/Hindi/Class_6/q81_hi.wav","/English/Class_6/q81_en.wav"},
  {"/Hindi/Class_6/q82_hi.wav","/English/Class_6/q82_en.wav"},
  {"/Hindi/Class_6/q83_hi.wav","/English/Class_6/q83_en.wav"},
  {"/Hindi/Class_6/q84_hi.wav","/English/Class_6/q84_en.wav"},
  {"/Hindi/Class_6/q85_hi.wav","/English/Class_6/q85_en.wav"},
  {"/Hindi/Class_6/q86_hi.wav","/English/Class_6/q86_en.wav"},
  {"/Hindi/Class_6/q87_hi.wav","/English/Class_6/q87_en.wav"},
  {"/Hindi/Class_6/q88_hi.wav","/English/Class_6/q88_en.wav"},
  {"/Hindi/Class_6/q89_hi.wav","/English/Class_6/q89_en.wav"},
  {"/Hindi/Class_6/q90_hi.wav","/English/Class_6/q90_en.wav"},
  {"/Hindi/Class_6/q91_hi.wav","/English/Class_6/q91_en.wav"},
  {"/Hindi/Class_6/q92_hi.wav","/English/Class_6/q92_en.wav"},
  {"/Hindi/Class_6/q93_hi.wav","/English/Class_6/q93_en.wav"},
  {"/Hindi/Class_6/q94_hi.wav","/English/Class_6/q94_en.wav"},
  {"/Hindi/Class_6/q95_hi.wav","/English/Class_6/q95_en.wav"},
  {"/Hindi/Class_6/q96_hi.wav","/English/Class_6/q96_en.wav"},
  {"/Hindi/Class_6/q97_hi.wav","/English/Class_6/q97_en.wav"},
  {"/Hindi/Class_6/q98_hi.wav","/English/Class_6/q98_en.wav"},
  {"/Hindi/Class_6/q99_hi.wav","/English/Class_6/q99_en.wav"},
};

const char* const quizQuestionsSeventh[129][SYS_LANG_COUNT] =
{
  {"/Hindi/Class_7/q1_hi.wav","/English/Class_7/q1_en.wav"},
  {"/Hindi/Class_7/q2_hi.wav","/English/Class_7/q2_en.wav"},
  {"/Hindi/Class_7/q3_hi.wav","/English/Class_7/q3_en.wav"},
  {"/Hindi/Class_7/q4_hi.wav","/English/Class_7/q4_en.wav"},
  {"/Hindi/Class_7/q5_hi.wav","/English/Class_7/q5_en.wav"},
  {"/Hindi/Class_7/q6_hi.wav","/English/Class_7/q6_en.wav"},
  {"/Hindi/Class_7/q7_hi.wav","/English/Class_7/q7_en.wav"},
  {"/Hindi/Class_7/q8_hi.wav","/English/Class_7/q8_en.wav"},
  {"/Hindi/Class_7/q9_hi.wav","/English/Class_7/q9_en.wav"},
  {"/Hindi/Class_7/q10_hi.wav","/English/Class_7/q10_en.wav"},
  {"/Hindi/Class_7/q11_hi.wav","/English/Class_7/q11_en.wav"},
  {"/Hindi/Class_7/q12_hi.wav","/English/Class_7/q12_en.wav"},
  {"/Hindi/Class_7/q13_hi.wav","/English/Class_7/q13_en.wav"},
  {"/Hindi/Class_7/q14_hi.wav","/English/Class_7/q14_en.wav"},
  {"/Hindi/Class_7/q15_hi.wav","/English/Class_7/q15_en.wav"},
  {"/Hindi/Class_7/q16_hi.wav","/English/Class_7/q16_en.wav"},
  {"/Hindi/Class_7/q17_hi.wav","/English/Class_7/q17_en.wav"},
  {"/Hindi/Class_7/q18_hi.wav","/English/Class_7/q18_en.wav"},
  {"/Hindi/Class_7/q19_hi.wav","/English/Class_7/q19_en.wav"},
  {"/Hindi/Class_7/q20_hi.wav","/English/Class_7/q20_en.wav"},
  {"/Hindi/Class_7/q21_hi.wav","/English/Class_7/q21_en.wav"},
  {"/Hindi/Class_7/q22_hi.wav","/English/Class_7/q22_en.wav"},
  {"/Hindi/Class_7/q23_hi.wav","/English/Class_7/q23_en.wav"},
  {"/Hindi/Class_7/q24_hi.wav","/English/Class_7/q24_en.wav"},
  {"/Hindi/Class_7/q25_hi.wav","/English/Class_7/q25_en.wav"},
  {"/Hindi/Class_7/q26_hi.wav","/English/Class_7/q26_en.wav"},
  {"/Hindi/Class_7/q27_hi.wav","/English/Class_7/q27_en.wav"},
  {"/Hindi/Class_7/q28_hi.wav","/English/Class_7/q28_en.wav"},
  {"/Hindi/Class_7/q29_hi.wav","/English/Class_7/q29_en.wav"},
  {"/Hindi/Class_7/q30_hi.wav","/English/Class_7/q30_en.wav"},
  {"/Hindi/Class_7/q31_hi.wav","/English/Class_7/q31_en.wav"},
  {"/Hindi/Class_7/q32_hi.wav","/English/Class_7/q32_en.wav"},
  {"/Hindi/Class_7/q33_hi.wav","/English/Class_7/q33_en.wav"},
  {"/Hindi/Class_7/q34_hi.wav","/English/Class_7/q34_en.wav"},
  {"/Hindi/Class_7/q35_hi.wav","/English/Class_7/q35_en.wav"},
  {"/Hindi/Class_7/q36_hi.wav","/English/Class_7/q36_en.wav"},
  {"/Hindi/Class_7/q37_hi.wav","/English/Class_7/q37_en.wav"},
  {"/Hindi/Class_7/q38_hi.wav","/English/Class_7/q38_en.wav"},
  {"/Hindi/Class_7/q39_hi.wav","/English/Class_7/q39_en.wav"},
  {"/Hindi/Class_7/q40_hi.wav","/English/Class_7/q40_en.wav"},
  {"/Hindi/Class_7/q41_hi.wav","/English/Class_7/q41_en.wav"},
  {"/Hindi/Class_7/q42_hi.wav","/English/Class_7/q42_en.wav"},
  {"/Hindi/Class_7/q43_hi.wav","/English/Class_7/q43_en.wav"},
  {"/Hindi/Class_7/q44_hi.wav","/English/Class_7/q44_en.wav"},
  {"/Hindi/Class_7/q45_hi.wav","/English/Class_7/q45_en.wav"},
  {"/Hindi/Class_7/q46_hi.wav","/English/Class_7/q46_en.wav"},
  {"/Hindi/Class_7/q47_hi.wav","/English/Class_7/q47_en.wav"},
  {"/Hindi/Class_7/q48_hi.wav","/English/Class_7/q48_en.wav"},
  {"/Hindi/Class_7/q49_hi.wav","/English/Class_7/q49_en.wav"},
  {"/Hindi/Class_7/q50_hi.wav","/English/Class_7/q50_en.wav"},
  {"/Hindi/Class_7/q51_hi.wav","/English/Class_7/q51_en.wav"},
  {"/Hindi/Class_7/q52_hi.wav","/English/Class_7/q52_en.wav"},
  {"/Hindi/Class_7/q53_hi.wav","/English/Class_7/q53_en.wav"},
  {"/Hindi/Class_7/q54_hi.wav","/English/Class_7/q54_en.wav"},
  {"/Hindi/Class_7/q55_hi.wav","/English/Class_7/q55_en.wav"},
  {"/Hindi/Class_7/q56_hi.wav","/English/Class_7/q56_en.wav"},
  {"/Hindi/Class_7/q57_hi.wav","/English/Class_7/q57_en.wav"},
  {"/Hindi/Class_7/q58_hi.wav","/English/Class_7/q58_en.wav"},
  {"/Hindi/Class_7/q59_hi.wav","/English/Class_7/q59_en.wav"},
  {"/Hindi/Class_7/q60_hi.wav","/English/Class_7/q60_en.wav"},
  {"/Hindi/Class_7/q61_hi.wav","/English/Class_7/q61_en.wav"},
  {"/Hindi/Class_7/q62_hi.wav","/English/Class_7/q62_en.wav"},
  {"/Hindi/Class_7/q63_hi.wav","/English/Class_7/q63_en.wav"},
  {"/Hindi/Class_7/q64_hi.wav","/English/Class_7/q64_en.wav"},
  {"/Hindi/Class_7/q65_hi.wav","/English/Class_7/q65_en.wav"},
  {"/Hindi/Class_7/q66_hi.wav","/English/Class_7/q66_en.wav"},
  {"/Hindi/Class_7/q67_hi.wav","/English/Class_7/q67_en.wav"},
  {"/Hindi/Class_7/q68_hi.wav","/English/Class_7/q68_en.wav"},
  {"/Hindi/Class_7/q69_hi.wav","/English/Class_7/q69_en.wav"},
  {"/Hindi/Class_7/q70_hi.wav","/English/Class_7/q70_en.wav"},
  {"/Hindi/Class_7/q71_hi.wav","/English/Class_7/q71_en.wav"},
  {"/Hindi/Class_7/q72_hi.wav","/English/Class_7/q72_en.wav"},
  {"/Hindi/Class_7/q73_hi.wav","/English/Class_7/q73_en.wav"},
  {"/Hindi/Class_7/q74_hi.wav","/English/Class_7/q74_en.wav"},
  {"/Hindi/Class_7/q75_hi.wav","/English/Class_7/q75_en.wav"},
  {"/Hindi/Class_7/q76_hi.wav","/English/Class_7/q76_en.wav"},
  {"/Hindi/Class_7/q77_hi.wav","/English/Class_7/q77_en.wav"},
  {"/Hindi/Class_7/q78_hi.wav","/English/Class_7/q78_en.wav"},
  {"/Hindi/Class_7/q79_hi.wav","/English/Class_7/q79_en.wav"},
  {"/Hindi/Class_7/q80_hi.wav","/English/Class_7/q80_en.wav"},
  {"/Hindi/Class_7/q81_hi.wav","/English/Class_7/q81_en.wav"},
  {"/Hindi/Class_7/q82_hi.wav","/English/Class_7/q82_en.wav"},
  {"/Hindi/Class_7/q83_hi.wav","/English/Class_7/q83_en.wav"},
  {"/Hindi/Class_7/q84_hi.wav","/English/Class_7/q84_en.wav"},
  {"/Hindi/Class_7/q85_hi.wav","/English/Class_7/q85_en.wav"},
  {"/Hindi/Class_7/q86_hi.wav","/English/Class_7/q86_en.wav"},
  {"/Hindi/Class_7/q87_hi.wav","/English/Class_7/q87_en.wav"},
  {"/Hindi/Class_7/q88_hi.wav","/English/Class_7/q88_en.wav"},
  {"/Hindi/Class_7/q89_hi.wav","/English/Class_7/q89_en.wav"},
  {"/Hindi/Class_7/q90_hi.wav","/English/Class_7/q90_en.wav"},
  {"/Hindi/Class_7/q91_hi.wav","/English/Class_7/q91_en.wav"},
  {"/Hindi/Class_7/q92_hi.wav","/English/Class_7/q92_en.wav"},
  {"/Hindi/Class_7/q93_hi.wav","/English/Class_7/q93_en.wav"},
  {"/Hindi/Class_7/q94_hi.wav","/English/Class_7/q94_en.wav"},
  {"/Hindi/Class_7/q95_hi.wav","/English/Class_7/q95_en.wav"},
  {"/Hindi/Class_7/q96_hi.wav","/English/Class_7/q96_en.wav"},
  {"/Hindi/Class_7/q97_hi.wav","/English/Class_7/q97_en.wav"},
  {"/Hindi/Class_7/q98_hi.wav","/English/Class_7/q98_en.wav"},
  {"/Hindi/Class_7/q99_hi.wav","/English/Class_7/q99_en.wav"},
  {"/Hindi/Class_7/q100_hi.wav","/English/Class_7/q100_en.wav"},
  {"/Hindi/Class_7/q101_hi.wav","/English/Class_7/q101_en.wav"},
  {"/Hindi/Class_7/q102_hi.wav","/English/Class_7/q102_en.wav"},
  {"/Hindi/Class_7/q103_hi.wav","/English/Class_7/q103_en.wav"},
  {"/Hindi/Class_7/q104_hi.wav","/English/Class_7/q104_en.wav"},
  {"/Hindi/Class_7/q105_hi.wav","/English/Class_7/q105_en.wav"},
  {"/Hindi/Class_7/q106_hi.wav","/English/Class_7/q106_en.wav"},
  {"/Hindi/Class_7/q107_hi.wav","/English/Class_7/q107_en.wav"},
  {"/Hindi/Class_7/q108_hi.wav","/English/Class_7/q108_en.wav"},
  {"/Hindi/Class_7/q109_hi.wav","/English/Class_7/q109_en.wav"},
  {"/Hindi/Class_7/q110_hi.wav","/English/Class_7/q110_en.wav"},
  {"/Hindi/Class_7/q111_hi.wav","/English/Class_7/q111_en.wav"},
  {"/Hindi/Class_7/q112_hi.wav","/English/Class_7/q112_en.wav"},
  {"/Hindi/Class_7/q113_hi.wav","/English/Class_7/q113_en.wav"},
  {"/Hindi/Class_7/q114_hi.wav","/English/Class_7/q114_en.wav"},
  {"/Hindi/Class_7/q115_hi.wav","/English/Class_7/q115_en.wav"},
  {"/Hindi/Class_7/q116_hi.wav","/English/Class_7/q116_en.wav"},
  {"/Hindi/Class_7/q117_hi.wav","/English/Class_7/q117_en.wav"},
  {"/Hindi/Class_7/q118_hi.wav","/English/Class_7/q118_en.wav"},
  {"/Hindi/Class_7/q119_hi.wav","/English/Class_7/q119_en.wav"},
  {"/Hindi/Class_7/q120_hi.wav","/English/Class_7/q120_en.wav"},
  {"/Hindi/Class_7/q121_hi.wav","/English/Class_7/q121_en.wav"},
  {"/Hindi/Class_7/q122_hi.wav","/English/Class_7/q122_en.wav"},
  {"/Hindi/Class_7/q123_hi.wav","/English/Class_7/q123_en.wav"},
  {"/Hindi/Class_7/q124_hi.wav","/English/Class_7/q124_en.wav"},
  {"/Hindi/Class_7/q125_hi.wav","/English/Class_7/q125_en.wav"},
  {"/Hindi/Class_7/q126_hi.wav","/English/Class_7/q126_en.wav"},
  {"/Hindi/Class_7/q127_hi.wav","/English/Class_7/q127_en.wav"},
  {"/Hindi/Class_7/q128_hi.wav","/English/Class_7/q128_en.wav"},
  {"/Hindi/Class_7/q129_hi.wav","/English/Class_7/q129_en.wav"},
};

String getQuizQuestionPath(int index) {
  switch (activeQuizClass) {
    case QUIZ_CLASS_5:
      if (index < 0 || index >= TOTAL_QUESTIONS_FIFTH) return String("");
      return getLocalizedAudio(quizQuestionsFifth[index]);
    case QUIZ_CLASS_6:
      if (index < 0 || index >= TOTAL_QUESTIONS_SIXTH) return String("");
      return getLocalizedAudio(quizQuestionsSixth[index]);
    case QUIZ_CLASS_7:
      if (index < 0 || index >= TOTAL_QUESTIONS_SEVENTH) return String("");
      return getLocalizedAudio(quizQuestionsSeventh[index]);
    default:
      if (index < 0 || index >= TOTAL_QUESTIONS) return String("");
      return getLocalizedAudio(quizQuestions[index]);
  }
}

bool isCorrectAnswer(int8_t answer, int questionIndex) {
  const std::vector<int8_t>* v;
  switch (activeQuizClass) {
    case QUIZ_CLASS_5: v = &correctAnswersFifth[questionIndex];   break;
    case QUIZ_CLASS_6: v = &correctAnswersSixth[questionIndex];   break;
    case QUIZ_CLASS_7: v = &correctAnswersSeventh[questionIndex]; break;
    default:           v = &correctAnswers[questionIndex];        break;
  }
  return std::find(v->begin(), v->end(), answer) != v->end();
}
int8_t getFirstCorrectAnswer(int questionIndex) {
  switch (activeQuizClass) {
    case QUIZ_CLASS_5: return correctAnswersFifth[questionIndex][0];
    case QUIZ_CLASS_6: return correctAnswersSixth[questionIndex][0];
    case QUIZ_CLASS_7: return correctAnswersSeventh[questionIndex][0];
    default:           return correctAnswers[questionIndex][0];
  }
}

int findNextSequentialQuestion() {
  int   total;
  bool* asked;
  switch (activeQuizClass) {
    case QUIZ_CLASS_5: total = TOTAL_QUESTIONS_FIFTH;   asked = questionsAskedFifth;   break;
    case QUIZ_CLASS_6: total = TOTAL_QUESTIONS_SIXTH;   asked = questionsAskedSixth;   break;
    case QUIZ_CLASS_7: total = TOTAL_QUESTIONS_SEVENTH; asked = questionsAskedSeventh; break;
    default:           total = TOTAL_QUESTIONS;         asked = questionsAsked;        break;
  }
  for (int i = 0; i < total; i++) {
    if (!asked[i]) return i;
  }
  return -1;
}

// Marks the current question as asked (in whichever bank is active) and
// bumps the running "questions asked" counter used for numbering.
void markQuestionAsked(int index) {
  if (index < 0) return;
  switch (activeQuizClass) {
    case QUIZ_CLASS_5:
      if (!questionsAskedFifth[index])   { questionsAskedFifth[index]   = true; totalQuestionsAsked++; }
      break;
    case QUIZ_CLASS_6:
      if (!questionsAskedSixth[index])   { questionsAskedSixth[index]   = true; totalQuestionsAsked++; }
      break;
    case QUIZ_CLASS_7:
      if (!questionsAskedSeventh[index]) { questionsAskedSeventh[index] = true; totalQuestionsAsked++; }
      break;
    default:
      if (!questionsAsked[index])        { questionsAsked[index]        = true; totalQuestionsAsked++; }
      break;
  }
}

// Clears the "asked" tracking array for whichever bank is currently active.
void resetAskedArray() {
  switch (activeQuizClass) {
    case QUIZ_CLASS_5: memset(questionsAskedFifth,   0, sizeof(questionsAskedFifth));   break;
    case QUIZ_CLASS_6: memset(questionsAskedSixth,   0, sizeof(questionsAskedSixth));   break;
    case QUIZ_CLASS_7: memset(questionsAskedSeventh, 0, sizeof(questionsAskedSeventh)); break;
    default:           memset(questionsAsked,        0, sizeof(questionsAsked));        break;
  }
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
  if (quizMode) {
    switch (activeQuizClass) {
      case QUIZ_CLASS_5: return getLocalizedAudio(rfidMode ? mlRfidQuizFifthAudioPaths   : mlSwitchQuizFifthAudioPaths);
      case QUIZ_CLASS_6: return getLocalizedAudio(rfidMode ? mlRfidQuizSixthAudioPaths   : mlSwitchQuizSixthAudioPaths);
      case QUIZ_CLASS_7: return getLocalizedAudio(rfidMode ? mlRfidQuizSeventhAudioPaths : mlSwitchQuizSeventhAudioPaths);
      default:           return getLocalizedAudio(rfidMode ? mlRfidQuizAudioPaths        : mlSwitchQuizAudioPaths);
    }
  }
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
void enterQuizModeClass(int classId);
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
  streamPaused = localPaused = true;
  deleteTextFile(SPEAKER_CONFIG_FILE);
  Serial.println("speaker.txt deleted. Restarting into setup portal...");
  delay(200);
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
  currentLanguage      = SYS_LANG_HINDI;
  quizMode             = false;
  activeQuizClass      = QUIZ_CLASS_NONE;
  rfidMode             = false;
  disableRFID();
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
  memset(questionsAskedFifth,   0, sizeof(questionsAskedFifth));
  memset(questionsAskedSixth,   0, sizeof(questionsAskedSixth));
  memset(questionsAskedSeventh, 0, sizeof(questionsAskedSeventh));
  totalQuestionsAsked  = 0;
  ledState = LED_ALL_OFF;
  write595_all(ledState);
  flashResetLEDs();
  resetAnswerLEDs();
  openAndStartAudio(String(switchPlayModeAudioPaths[SYS_LANG_HINDI]), true);
}

// =====================================================
// Quiz mode
// =====================================================
void enterQuizModeCommon(int classId) {
  if (classId == QUIZ_CLASS_NONE) Serial.println("\n=== QUIZ MODE ===");
  else { Serial.print("\n=== QUIZ MODE (CLASS "); Serial.print(classId); Serial.println(") ==="); }
  safeCloseFile(audioFile);
  streamPaused = localPaused = true;
  quizMode        = true;
  activeQuizClass = (QuizClass)classId;
  resetAskedArray();
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
void enterQuizMode()             { enterQuizModeCommon(QUIZ_CLASS_NONE); }
void enterQuizModeClass(int classId) { enterQuizModeCommon(classId); }

void playQuestion() {
  if (!quizMode || currentQuestionIndex < 0) return;
  markQuestionAsked(currentQuestionIndex);
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
    resetAskedArray();
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
  activeQuizClass      = QUIZ_CLASS_NONE;
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
  const bool reverseScoreNumbers = (currentLanguage == SYS_LANG_HINDI);
  switch (scoreSequence) {
    case SEQ_SCORE_INTRO: {
      if (openAndStartAudio(getLocalizedAudio(scoreIntroAudioPaths)))
        scoreSequence = reverseScoreNumbers ? SEQ_TOTAL_NUMBER : SEQ_CORRECT_NUMBER;
      else {
        scoreSequence = reverseScoreNumbers ? SEQ_TOTAL_NUMBER : SEQ_CORRECT_NUMBER;
        continueScoreAnnouncement();
      }
      break;
    }
    case SEQ_CORRECT_NUMBER: {
      String f = getNumberAudioPath(correctAnswersCount);
      if (f.length() && openAndStartAudio(f)) scoreSequence = reverseScoreNumbers ? SEQ_CORRECT_WORD : SEQ_OUT_OF;
      else { scoreSequence = SEQ_DONE; continueScoreAnnouncement(); }
      break;
    }
    case SEQ_OUT_OF: {
      if (openAndStartAudio(getLocalizedAudio(outOfAudioPaths)))
        scoreSequence = reverseScoreNumbers ? SEQ_CORRECT_NUMBER : SEQ_TOTAL_NUMBER;
      else {
        scoreSequence = reverseScoreNumbers ? SEQ_CORRECT_NUMBER : SEQ_TOTAL_NUMBER;
        continueScoreAnnouncement();
      }
      break;
    }
    case SEQ_TOTAL_NUMBER: {
      String f = getNumberAudioPath(totalQuestionsAnswered);
      if (f.length() && openAndStartAudio(f)) scoreSequence = reverseScoreNumbers ? SEQ_OUT_OF : SEQ_CORRECT_WORD;
      else {
        scoreSequence = reverseScoreNumbers ? SEQ_OUT_OF : SEQ_CORRECT_WORD;
        continueScoreAnnouncement();
      }
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
  unsigned long quizButtonLastPressTime = 0;
  bool          quizButtonPendingSingle = false;
  unsigned long quizButtonPressTime     = 0;

  // Class-selection window: opened by a quiz-button double-click. While
  // open, Reset/Mode/Lang pick Class 5/6/7 instead of doing their normal
  // job (board reset / RFID toggle / language toggle).
  bool          waitingForClassSelection = false;
  unsigned long classSelectionStartTime  = 0;
  const unsigned long CLASS_SELECTION_TIMEOUT = 25000; // 8s to pick a class

  for (;;) {
    uint64_t stable;
    if (xQueueReceive(buttonQueue, &stable, pdMS_TO_TICKS(50))) {
      currentStable = stable;
      uint64_t pressed  = stable & ~prevStable;
      uint64_t released = prevStable & ~stable;

      if (pressed & (1ULL << QUIZ_BUTTON_NUM)) {
        unsigned long nowPress = millis();
        if (quizButtonPendingSingle && (nowPress - quizButtonLastPressTime) <= DOUBLE_PRESS_WINDOW) {
          // Double-click -> open the "pick a class" selection window.
          // Reset/Mode/Lang will pick Class 5/6/7 on their next press.
          quizButtonPendingSingle = false;
          if (quizMode) exitQuizMode();
          waitingForClassSelection = true;
          classSelectionStartTime  = nowPress;
          Serial.println("\n=== SELECT CLASS: Reset=5, Mode=6, Lang=7 ===");
          ledState = LED_ALL_ON;
          write595_all(ledState);
          openAndStartAudio(getLocalizedAudio(classSelectPromptAudioPaths), true);
        } else {
          quizButtonPendingSingle = true;
          quizButtonPressTime     = nowPress;
        }
        quizButtonLastPressTime = nowPress;
      }

      if (waitingForClassSelection) {
        // Consume Reset/Mode/Lang presses as the class pick instead of
        // letting them fall through to their normal behavior below.
        if (pressed & (1ULL << RESET_BUTTON_NUM)) {
          waitingForClassSelection = false;
          enterQuizModeClass(QUIZ_CLASS_5);
        } else if (pressed & (1ULL << MODE_BUTTON_NUM)) {
          waitingForClassSelection = false;
          enterQuizModeClass(QUIZ_CLASS_6);
        } else if (pressed & (1ULL << LANG_BUTTON_NUM)) {
          waitingForClassSelection = false;
          enterQuizModeClass(QUIZ_CLASS_7);
        }
      } else {
        if (pressed & (1ULL << RESET_BUTTON_NUM)) {
          resetButtonPressTime = millis();
          resetButtonHeld      = true;
          resetHoldFired       = false;
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
      }

      if (released & (1ULL << RESET_BUTTON_NUM)) {
        if (resetButtonHeld && !resetHoldFired) {
          resetBoard();
        }
        resetButtonHeld = false;
      }

      for (uint8_t i = 0; i < 40; i++) {
        if (!(pressed & (1ULL << i))) continue;
        if (i == QUIZ_BUTTON_NUM || i == RESET_BUTTON_NUM || i == MODE_BUTTON_NUM || i == LANG_BUTTON_NUM) continue;
        if (rfidMode) continue;
        if (waitingForClassSelection) continue; // ignore state buttons while picking a class

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
        safeCloseFile(audioFile);
        openAndStartAudio(getLocalizedAudio(speakerSetupAudioPaths), true);
        delay(4000);
        deleteSpeakerConfigAndRestart();
      }
    }

    if (quizButtonPendingSingle && (millis() - quizButtonPressTime > DOUBLE_PRESS_WINDOW)) {
      quizButtonPendingSingle = false;
      quizMode ? exitQuizMode() : enterQuizMode();
    }

    if (waitingForClassSelection && (millis() - classSelectionStartTime > CLASS_SELECTION_TIMEOUT)) {
      // Nobody picked a class in time; back out quietly to normal mode.
      waitingForClassSelection = false;
      Serial.println("Class selection timed out");
      ledState = LED_ALL_OFF;
      write595_all(ledState);
      openAndStartAudio(getModeAnnouncement(), true);
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
  Serial.println("   BHARAT QUIZ BOARD  —  HINDI + ENGLISH        ");
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
