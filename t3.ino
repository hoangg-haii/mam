#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <time.h>
#include <sys/time.h>

// UUIDs cho BLE Service và Characteristic
#define SERVICE_UUID        "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

// L298N số 1 (Bên trái)
const int IN1_L1 = 27;
const int IN2_L1 = 26;
const int IN3_L1 = 25;
const int IN4_L1 = 33;

// L298N số 2 (Bên phải)
const int IN1_L2 = 19;
const int IN2_L2 = 18;
const int IN3_L2 = 17;
const int IN4_L2 = 16;

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Quản lý hẹn giờ
int alarmH = -1, alarmM = -1, alarmS = -1;
char alarmDir = ' '; // 'L' cho trái, 'R' cho phải
bool isAlarmSet = false;

/**
 * Cập nhật thời gian hệ thống ESP32
 * Chấp nhận lệnh từ App dạng: "SHHMMSS" hoặc "STHHMMSS"
 */
void updateInternalTime(String timeData) {
  int startIndex = timeData.startsWith("ST") ? 2 : 1;
  String digits = timeData.substring(startIndex);

  if (digits.length() == 6) {
    int h = digits.substring(0, 2).toInt();
    int m = digits.substring(2, 4).toInt();
    int s = digits.substring(4, 6).toInt();

    if (h >= 0 && h < 24 && m >= 0 && m < 60 && s >= 0 && s < 60) {
      struct tm tm;
      memset(&tm, 0, sizeof(struct tm));
      
      tm.tm_year = 2024 - 1900; 
      tm.tm_mon = 0;
      tm.tm_mday = 1;
      tm.tm_hour = h;
      tm.tm_min = m;
      tm.tm_sec = s;
      tm.tm_isdst = 0;

      time_t t = mktime(&tm);
      
      if (t != (time_t)-1) {
        struct timeval now = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&now, NULL);
        Serial.printf(">>> DA DONG BO GIO TU APP: %02d:%02d:%02d\n", h, m, s);
      }
    }
  }
}

void stopAll();
void moveForward();
void moveBackward();
void leftForward();
void leftBackward();
void rightForward();
void rightBackward();
void executeAlarm();

void handleCommand(String cmd) {
  cmd.trim();
  
  // 1. Lệnh đồng bộ giờ: S + 6 chữ số hoặc ST + 6 chữ số
  if (cmd.startsWith("S") && isDigit(cmd[1])) {
    updateInternalTime(cmd);
    return;
  }

  // 2. Lệnh hẹn giờ mới: HHMMSSR hoặc HHMMSSL (7 ký tự)
  if (cmd.length() == 7 && isDigit(cmd[0])) {
    alarmH = cmd.substring(0, 2).toInt();
    alarmM = cmd.substring(2, 4).toInt();
    alarmS = cmd.substring(4, 6).toInt();
    alarmDir = toupper(cmd[6]); // Lấy ký tự cuối cùng 'L' hoặc 'R'
    isAlarmSet = true;
    Serial.printf("Da nhan hen gio: %02d:%02d:%02d - Huong: %c\n", alarmH, alarmM, alarmS, alarmDir);
  } 
  // 3. Lệnh hẹn giờ cũ (6 chữ số - mặc định chạy thẳng hoặc không hướng)
  else if (cmd.length() == 6 && isDigit(cmd[0])) {
    alarmH = cmd.substring(0, 2).toInt();
    alarmM = cmd.substring(2, 4).toInt();
    alarmS = cmd.substring(4, 6).toInt();
    alarmDir = 'F'; // 'F' mặc định cho Forward nếu không có hậu tố L/R
    isAlarmSet = true;
    Serial.printf("Da nhan hen gio: %02d:%02d:%02d - Mac dinh: Thang\n", alarmH, alarmM, alarmS);
  }
  // 4. Các lệnh điều khiển thủ công
  else {
    char c = cmd[0];
    switch(c) {
      case 't': moveForward(); break;
      case 'l': moveBackward(); break;
      case '1': leftForward(); break;
      case '2': leftBackward(); break;
      case '3': rightForward(); break;
      case '4': rightBackward(); break;
      case 'd': stopAll(); break;
    }
  }
}

class RxCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    String val = c->getValue(); 
    if (val.length() > 0) {
      Serial.print("BLE Nhan: ");
      Serial.println(val);
      handleCommand(val);
    }
  }
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println(">>> Da ket noi!");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println(">>> Mat ket noi.");
    }
};

void setup() {
  Serial.begin(115200);

  // Thiet lap mui gio UTC0
  setenv("TZ", "UTC0", 1); 
  tzset();

  pinMode(IN1_L1, OUTPUT); pinMode(IN2_L1, OUTPUT);
  pinMode(IN3_L1, OUTPUT); pinMode(IN4_L1, OUTPUT);
  pinMode(IN1_L2, OUTPUT); pinMode(IN2_L2, OUTPUT);
  pinMode(IN3_L2, OUTPUT); pinMode(IN4_L2, OUTPUT);
  stopAll();

  BLEDevice::init("ESP32_Motor_Control");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_WRITE_NR |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new RxCallback());
  pCharacteristic->setValue("READY");

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);
  BLEDevice::startAdvertising();

  Serial.println("ESP32 System Ready (Strict Sync Mode).");
}

void loop() {
  time_t now_ts;
  struct tm timeinfo;
  time(&now_ts);
  gmtime_r(&now_ts, &timeinfo);

  static uint32_t lastTick = 0;
  if (millis() - lastTick > 1000) {
    char timeStr[25];
    sprintf(timeStr, "TIME:%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    Serial.println(timeStr);
    
    if (deviceConnected) {
      pCharacteristic->setValue(timeStr);
      pCharacteristic->notify();
    }
    
    lastTick = millis();
    
    if (isAlarmSet && 
        timeinfo.tm_hour == alarmH && 
        timeinfo.tm_min == alarmM && 
        timeinfo.tm_sec == alarmS) {
      executeAlarm();
    }
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    handleCommand(cmd);
  }

  if (!deviceConnected && oldDeviceConnected) {
    delay(500); 
    BLEDevice::startAdvertising(); 
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
}

void executeAlarm() {
  Serial.printf(">>> Bat dau chay hen gio huong: %c\n", alarmDir);
  
  if (alarmDir == 'L') {
    leftForward(); // Hoặc rẽ trái tùy theo logic phần cứng của bạn
  } else if (alarmDir == 'R') {
    rightForward(); // Hoặc rẽ phải
  } else {
    moveForward(); // Mặc định chạy thẳng
  }
  
  delay(1000); // Chạy trong 3 giây
  stopAll();
  isAlarmSet = false;
  alarmDir = ' ';
}

void moveForward() {
  digitalWrite(IN1_L1, HIGH); digitalWrite(IN2_L1, LOW); digitalWrite(IN3_L1, HIGH); digitalWrite(IN4_L1, LOW);
  digitalWrite(IN1_L2, HIGH); digitalWrite(IN2_L2, LOW); digitalWrite(IN3_L2, HIGH); digitalWrite(IN4_L2, LOW);
}

void moveBackward() {
  digitalWrite(IN1_L1, LOW); digitalWrite(IN2_L1, HIGH); digitalWrite(IN3_L1, LOW); digitalWrite(IN4_L1, HIGH);
  digitalWrite(IN1_L2, LOW); digitalWrite(IN2_L2, HIGH); digitalWrite(IN3_L2, LOW); digitalWrite(IN4_L2, HIGH);
}

void leftForward() { digitalWrite(IN1_L1, HIGH); digitalWrite(IN2_L1, LOW); digitalWrite(IN3_L1, HIGH); digitalWrite(IN4_L1, LOW); }
void leftBackward() { digitalWrite(IN1_L1, LOW); digitalWrite(IN2_L1, HIGH); digitalWrite(IN3_L1, LOW); digitalWrite(IN4_L1, HIGH); }
void rightForward() { digitalWrite(IN1_L2, HIGH); digitalWrite(IN2_L2, LOW); digitalWrite(IN3_L2, HIGH); digitalWrite(IN4_L2, LOW); }
void rightBackward() { digitalWrite(IN1_L2, LOW); digitalWrite(IN2_L2, HIGH); digitalWrite(IN3_L2, LOW); digitalWrite(IN4_L2, HIGH); }

void stopAll() {
  digitalWrite(IN1_L1, LOW); digitalWrite(IN2_L1, LOW); digitalWrite(IN3_L1, LOW); digitalWrite(IN4_L1, LOW);
  digitalWrite(IN1_L2, LOW); digitalWrite(IN2_L2, LOW); digitalWrite(IN3_L2, LOW); digitalWrite(IN4_L2, LOW);
}