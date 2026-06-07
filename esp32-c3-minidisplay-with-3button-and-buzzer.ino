#include <Arduino.h>
#include <U8g2lib.h>
#include <stdio.h>

// [1] OLED 핀 및 설정 정의 (하드웨어 특성 고정)
#define OLED_RESET U8X8_PIN_NONE  
#define OLED_SDA 5
#define OLED_SCL 6

// 하드웨어 I2C 객체 생성
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RESET, OLED_SCL, OLED_SDA);

// 하드웨어 물리 특성값 고정
static constexpr int viewWidth = 70;         
static constexpr int viewHeight = 40;        
static constexpr int xOffset = (128 - 72) / 2;    // 28 고정
static constexpr int yOffset = (64 - 40) / 2 + 12; // 24 고정

// [2] 버튼, 부저, 가변저항 핀 정의
const int BUTTON_1 = 0;   
const int BUTTON_2 = 1;   
const int BUTTON_3 = 2;   
const int POT_PIN = 3;       // 가변저항 가운데 핀 (ADC1_CH3)
const int BUZZER_PIN = 10;

// 버튼 상태 관리 변수
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; 
int lastPressedButton = 0; 
int uptimeCounter = 0;     
unsigned long lastUptimeTime = 0;

// 💡 가변저항 노이즈 필터링 변수 선언
int potValue0to100 = 0;      // 최종 저장용 가변저항 값 (0~100)
unsigned long lastPotReadTime = 0;
const unsigned long potReadInterval = 50; // 아날로그 샘플링 주기 (50ms로 단축하여 반응성 향상)

const int numReadings = 20;    // 평균을 낼 데이터 개수 (높일수록 부드러워지나 미세한 딜레이 발생)
int readings[numReadings];      // 아날로그 로우(Raw) 값을 담을 배열
int readIndex = 0;              // 현재 배열 인덱스
long potTotal = 0;              // 누적 합계 변수 (Long 타입으로 오버플로우 방지)
int averageAnalog = 0;          // 평균 아날로그 값

// [3] 부저 비프음 출력 함수 (낮은 주파수 유지)
void beep(int duration_ms, int count = 1, int frequency_hz = 1000) {
  for (int i = 0; i < count; i++) {
    tone(BUZZER_PIN, frequency_hz); 
    delay(duration_ms);
    noTone(BUZZER_PIN);             
    
    if (count > 1 && i < count - 1) {
      delay(100); 
    }
  }
}

// [4] OLED 디스플레이 출력 함수
void handle_oled(int uptime, int buttonNum, int potVal) {
  u8g2.clearBuffer();
  
  // 1. "HELLO" 폰트 사이즈 축소 및 출력 위치 보정 (12px 높이)
  u8g2.setFont(u8g2_font_9x15_tf); 
  u8g2.drawStr(xOffset, yOffset + 12, "HELLO"); 
  
  // 2. 작은 폰트로 부가 정보 출력
  u8g2.setFont(u8g2_font_4x6_tr);
  
  // 라인 1: 가변저항 값 표시 (0~100)
  char potBuffer[20];
  snprintf(potBuffer, sizeof(potBuffer), "Pot: %d%%", potVal);
  u8g2.drawStr(xOffset, yOffset + 22, potBuffer);
  
  // 라인 2: 업타임 출력
  char uptimeBuffer[20];
  snprintf(uptimeBuffer, sizeof(uptimeBuffer), "Uptime: %ds", uptime);
  u8g2.drawStr(xOffset, yOffset + 31, uptimeBuffer);
  
  // 라인 3: 마지막으로 누른 버튼 출력
  char btnBuffer[20];
  if (buttonNum > 0) {
    snprintf(btnBuffer, sizeof(btnBuffer), "Last Btn: P%d", buttonNum);
  } else {
    snprintf(btnBuffer, sizeof(btnBuffer), "Last Btn: None");
  }
  u8g2.drawStr(xOffset, yOffset + 40, btnBuffer);
  
  u8g2.sendBuffer();
}

void setup(void) {
  Serial.begin(115200);
  
  // OLED 초기화
  u8g2.begin();
  u8g2.setContrast(255);     
  u8g2.setBusClock(400000);  
  
  // 버튼 핀 설정 (내부 풀업)
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(BUTTON_3, INPUT_PULLUP);

  // 가변저항 핀 설정
  pinMode(POT_PIN, INPUT);

  // 💡 필터 배열 초기화
  for (int thisReading = 0; thisReading < numReadings; thisReading++) {
    readings[thisReading] = 0;
  }

  Serial.println("ESP32-C3 Filtered System Initialized!");
  
  // 최초 아날로그 기본값 수집 채우기
  int initialRaw = analogRead(POT_PIN);
  for (int i = 0; i < numReadings; i++) {
    readings[i] = initialRaw;
  }
  potTotal = (long)initialRaw * numReadings;
  potValue0to100 = map(initialRaw, 0, 4095, 0, 100);

  // 첫 화면 출력
  handle_oled(uptimeCounter, lastPressedButton, potValue0to100);
}

void loop(void) {
  unsigned long currentTime = millis();

  // 1초마다 업타임 카운트 증가 및 화면 갱신
  if (currentTime - lastUptimeTime >= 1000) {
    uptimeCounter++;
    lastUptimeTime = currentTime;
    handle_oled(uptimeCounter, lastPressedButton, potValue0to100);
  }

  // 💡 가변저항 주기적 샘플링 및 이동 평균 필터링 처리
  if (currentTime - lastPotReadTime >= potReadInterval) {
    lastPotReadTime = currentTime;
    
    // 1. 가장 오래된 데이터를 합계에서 차감
    potTotal = potTotal - readings[readIndex];
    // 2. 현재 아날로그 데이터 새로 읽기
    readings[readIndex] = analogRead(POT_PIN);
    // 3. 읽은 값을 다시 합계에 추가
    potTotal = potTotal + readings[readIndex];
    // 4. 인덱스 순환 처리
    readIndex = readIndex + 1;
    if (readIndex >= numReadings) {
      readIndex = 0;
    }

    // 5. 평균값 연산 후 0~100 스케일링
    averageAnalog = potTotal / numReadings;
    int newPotVal = map(averageAnalog, 0, 4095, 0, 100); 
    
    // 6. 💡 미세 떨림 마진 적용 (1 이상의 변화가 확실히 생겼을 때만 화면 갱신)
    if (abs(newPotVal - potValue0to100) >= 1) { 
      potValue0to100 = newPotVal;
      handle_oled(uptimeCounter, lastPressedButton, potValue0to100);
    }
  }

  // 버튼 입력 감지 및 디바운스
  if ((currentTime - lastDebounceTime) > debounceDelay) {
    if (digitalRead(BUTTON_1) == LOW) {
      Serial.println("Button 1 Pressed");
      lastPressedButton = 1;
      beep(100, 1, 800); 
      handle_oled(uptimeCounter, lastPressedButton, potValue0to100); 
      lastDebounceTime = currentTime;
    }
    else if (digitalRead(BUTTON_2) == LOW) {
      Serial.println("Button 2 Pressed");
      lastPressedButton = 2;
      beep(400, 1, 600); 
      handle_oled(uptimeCounter, lastPressedButton, potValue0to100);
      lastDebounceTime = currentTime;
    }
    else if (digitalRead(BUTTON_3) == LOW) {
      Serial.println("Button 3 Pressed");
      lastPressedButton = 3;
      beep(100, 2, 1000); 
      handle_oled(uptimeCounter, lastPressedButton, potValue0to100);
      lastDebounceTime = currentTime;
    }
  }
}
